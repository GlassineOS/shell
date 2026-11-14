// SPDX-License-Identifier: GPL-2.0-or-later
// glassine-predict-build: builds a base model file (src/predict/gpm.h) from plain text.
// A host tool: the images run only the result. Deterministic: the same corpus files and
// options give the same bytes.
//
//   glassine-predict-build [options] LANG OUT CORPUS[:WEIGHT]...
//     -v N   at most N words (default 60000)
//     -m N   a word needs N uses to be in the vocabulary (default 3)
//     -s L   single-letter words allowed, folded, e.g. "ai" (default none)
//     -b N   keep N next words per word (default 24), seen at least -B times (default 2)
//     -t N   keep N next words per word pair (default 8), seen at least -T times
//            (default 3), for pairs seen at least -P times (default 8)
//
// CORPUS: UTF-8 text, any line length; each line starts a sentence. WEIGHT (an integer,
// default 1) multiplies that file's counts.
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpm.h"
#include "text.h"

static void die(const char *m) { fprintf(stderr, "glassine-predict-build: %s\n", m); exit(1); }
static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) die("out of memory"); return p; }
static void *xcalloc(size_t a, size_t b) { void *p = calloc(a ? a : 1, b); if (!p) die("out of memory"); return p; }
static void *xrealloc(void *p, size_t n) { p = realloc(p, n); if (!p) die("out of memory"); return p; }

/* ---- string table: exact spelling -> count, or folded key -> id */
struct sent { char *s; uint32_t len; uint32_t aux; uint64_t count, mid; };  /* mid: uses not at a sentence start */
struct stab { struct sent *e; size_t cap, n; };

static uint64_t fnv(const char *s, size_t n)
{
	uint64_t h = 1469598103934665603ULL;
	for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 1099511628211ULL;
	return h;
}

static void stab_grow(struct stab *t);
static struct sent *stab_get(struct stab *t, const char *s, size_t n, int add)
{
	if (add && (t->n + 1) * 10 > t->cap * 7) stab_grow(t);
	if (!t->cap) return NULL;
	size_t m = t->cap - 1, i = fnv(s, n) & m;
	for (;; i = (i + 1) & m) {
		struct sent *e = &t->e[i];
		if (!e->s) {
			if (!add) return NULL;
			e->s = xmalloc(n + 1); memcpy(e->s, s, n); e->s[n] = 0; e->len = (uint32_t)n;
			t->n++;
			return e;
		}
		if (e->len == n && !memcmp(e->s, s, n)) return e;
	}
}

static void stab_grow(struct stab *t)
{
	struct stab o = *t;
	t->cap = o.cap ? o.cap * 2 : 1 << 16;
	t->e = xcalloc(t->cap, sizeof *t->e);
	t->n = 0;
	for (size_t i = 0; i < o.cap; i++)
		if (o.e[i].s) {
			size_t m = t->cap - 1, j = fnv(o.e[i].s, o.e[i].len) & m;
			while (t->e[j].s) j = (j + 1) & m;
			t->e[j] = o.e[i];
			t->n++;
		}
	free(o.e);
}

/* ---- n-gram table: packed ids -> count */
struct ntab { uint64_t *k; uint32_t *c; size_t cap, n; };

static void ntab_add(struct ntab *t, uint64_t key, uint32_t w)
{
	key++;
	if ((t->n + 1) * 10 > t->cap * 7) {
		struct ntab o = *t;
		t->cap = o.cap ? o.cap * 2 : 1 << 20;
		t->k = xcalloc(t->cap, 8); t->c = xcalloc(t->cap, 4); t->n = 0;
		for (size_t i = 0; i < o.cap; i++)
			if (o.k[i]) {
				size_t m = t->cap - 1, j = (o.k[i] * 0x9E3779B97F4A7C15ULL >> 17) & m;
				while (t->k[j]) j = (j + 1) & m;
				t->k[j] = o.k[i]; t->c[j] = o.c[i]; t->n++;
			}
		free(o.k); free(o.c);
	}
	size_t m = t->cap - 1, i = (key * 0x9E3779B97F4A7C15ULL >> 17) & m;
	while (t->k[i] && t->k[i] != key) i = (i + 1) & m;
	if (!t->k[i]) { t->k[i] = key; t->n++; }
	t->c[i] = t->c[i] > UINT32_MAX - w ? UINT32_MAX : t->c[i] + w;
}

/* ---- corpus reading */
struct corpus { const char *path; uint32_t weight; };

typedef void (*line_fn)(const char *s, size_t n, uint32_t w, void *ctx);
static void each_line(const struct corpus *c, line_fn fn, void *ctx)
{
	FILE *f = fopen(c->path, "rb");
	if (!f) { fprintf(stderr, "%s: %s\n", c->path, strerror(errno)); exit(1); }
	char *line = NULL; size_t cap = 0; ssize_t n;
	while ((n = getline(&line, &cap, f)) > 0) fn(line, (size_t)n, c->weight, ctx);
	free(line);
	fclose(f);
}

static char single_ok[64];
static int word_ok(const char *fold, size_t n)
{
	uint32_t c;
	int l = pt_u8dec((const unsigned char *)fold, n, &c);
	if ((size_t)l == n)     /* one letter */
		return c < 0x80 && strchr(single_ok, (int)c) != NULL;
	return 1;
}

static void pass1(const char *s, size_t n, uint32_t w, void *ctx)
{
	struct stab *spell = ctx;
	size_t pos = 0, ws, wl;
	int k, first = 1;
	while ((k = pt_next(s, n, &pos, &ws, &wl, 0)) != PT_END) {
		if (k == PT_SENT) { first = 1; continue; }
		if (k == PT_WORD) {
			/* normalize apostrophes in the spelling too */
			char b[PT_WORD_MAX * 2 + 8]; size_t o = 0, i = 0;
			while (i < wl) {
				uint32_t c;
				i += pt_u8dec((const unsigned char *)s + ws + i, wl - i, &c);
				o += pt_u8enc(pt_is_apostrophe(c) ? '\'' : c, b + o);
			}
			struct sent *e = stab_get(spell, b, o, 1);
			e->count += w;
			if (!first) e->mid += w;
		}
		first = 0;
	}
}

struct p2 { struct stab *keys; struct ntab bi, tri; uint32_t bos; uint64_t ntok; };
#define NONE UINT32_MAX

static void pass2(const char *s, size_t n, uint32_t w, void *ctx)
{
	struct p2 *p = ctx;
	uint32_t h2 = NONE, h1 = p->bos;
	size_t pos = 0, ws, wl;
	int k;
	while ((k = pt_next(s, n, &pos, &ws, &wl, 0)) != PT_END) {
		if (k == PT_SENT) { h2 = NONE; h1 = p->bos; continue; }
		if (k == PT_BREAK) { h2 = h1 = NONE; continue; }
		char f[PT_WORD_MAX * 2 + 8];
		size_t fl = pt_fold_str(s + ws, wl, f);
		struct sent *e = stab_get(p->keys, f, fl, 0);
		uint32_t id = e ? e->aux : NONE;
		if (id != NONE) {
			p->ntok += w;
			if (h1 != NONE) {
				ntab_add(&p->bi, (uint64_t)h1 << 21 | id, w);
				if (h2 != NONE) ntab_add(&p->tri, (uint64_t)h2 << 42 | (uint64_t)h1 << 21 | id, w);
			}
		}
		h2 = h1; h1 = id;
	}
}

/* ---- pruning: (context, word, count) rows */
struct row { uint64_t ctx; uint32_t id, cnt; };
static int row_cmp(const void *a, const void *b)
{
	const struct row *x = a, *y = b;
	if (x->ctx != y->ctx) return x->ctx < y->ctx ? -1 : 1;
	if (x->cnt != y->cnt) return x->cnt > y->cnt ? -1 : 1;
	return x->id < y->id ? -1 : x->id > y->id;
}

static uint8_t quant(double p)
{
	if (p <= 0) return 255;
	double q = -8.0 * log2(p);
	return q < 0 ? 0 : q > 255 ? 255 : (uint8_t)lround(q);
}

struct vword { char *key, *disp; uint64_t count; uint64_t dcount, dmid; };
static int vw_count_cmp(const void *a, const void *b)
{
	const struct vword *x = a, *y = b;
	if (x->count != y->count) return x->count > y->count ? -1 : 1;
	return strcmp(x->key, y->key);
}
static int sp_cmp(const void *a, const void *b) { return strcmp((*(struct sent *const *)a)->s, (*(struct sent *const *)b)->s); }
static int vw_key_cmp(const void *a, const void *b) { return strcmp(((const struct vword *)a)->key, ((const struct vword *)b)->key); }

/* rows grouped by context -> successor lists (top `keep`, at least `minc`), totals from all rows */
static size_t prune(struct row *r, size_t n, uint32_t keep, uint32_t minc, uint32_t ctxmin,
		    uint64_t **ctxs, uint32_t **starts, uint32_t **succ, size_t *nsucc)
{
	qsort(r, n, sizeof *r, row_cmp);
	size_t nc = 0, ns = 0;
	*ctxs = xmalloc((n + 1) * 8); *starts = xmalloc((n + 1) * 4); *succ = xmalloc((n + 1) * 4);
	for (size_t i = 0; i < n;) {
		size_t j = i; uint64_t tot = 0;
		while (j < n && r[j].ctx == r[i].ctx) tot += r[j++].cnt;
		if (tot >= ctxmin) {
			size_t st = ns;
			for (size_t m = i; m < j && ns - st < keep && r[m].cnt >= minc; m++)
				(*succ)[ns++] = r[m].id << 8 | quant((double)r[m].cnt / (double)tot);
			if (ns > st) { (*ctxs)[nc] = r[i].ctx; (*starts)[nc] = (uint32_t)st; nc++; }
		}
		i = j;
	}
	*nsucc = ns;
	return nc;
}

static void put(FILE *f, const void *p, size_t n) { if (n && fwrite(p, 1, n, f) != n) die("write failed"); }
static void align4(FILE *f) { static const char z[4]; long o = ftell(f); if (o % 4) put(f, z, 4 - o % 4); }

int main(int argc, char **argv)
{
	uint32_t vmax = 60000, vmin = 3, bik = 24, bimin = 2, trik = 8, trimin = 3, tripair = 8;
	int i = 1;
	for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		char o = argv[i][1];
		if (i + 1 >= argc) die("missing option value");
		const char *v = argv[++i];
		switch (o) {
		case 'v': vmax = (uint32_t)atol(v); break;
		case 'm': vmin = (uint32_t)atol(v); break;
		case 's': snprintf(single_ok, sizeof single_ok, "%s", v); break;
		case 'b': bik = (uint32_t)atol(v); break;
		case 'B': bimin = (uint32_t)atol(v); break;
		case 't': trik = (uint32_t)atol(v); break;
		case 'T': trimin = (uint32_t)atol(v); break;
		case 'P': tripair = (uint32_t)atol(v); break;
		default: die("unknown option");
		}
	}
	if (argc - i < 3) die("usage: glassine-predict-build [options] LANG OUT CORPUS[:WEIGHT]...");
	if (vmax == 0 || vmax >= (1u << 21) - 1) die("bad -v");
	const char *lang = argv[i], *out = argv[i + 1];
	if (strlen(lang) >= 8) die("language tag too long");
	size_t ncorp = (size_t)(argc - i - 2);
	struct corpus *cs = xcalloc(ncorp, sizeof *cs);
	for (size_t c = 0; c < ncorp; c++) {
		char *a = argv[i + 2 + c], *colon = strrchr(a, ':');
		cs[c].path = a; cs[c].weight = 1;
		if (colon && colon[1] && strspn(colon + 1, "0123456789") == strlen(colon + 1)) {
			*colon = 0; cs[c].weight = (uint32_t)atol(colon + 1);
			if (!cs[c].weight) die("weight 0");
		}
	}

	/* pass 1: spellings, then folded words with their most used spelling */
	struct stab spell = {0};
	for (size_t c = 0; c < ncorp; c++) each_line(&cs[c], pass1, &spell);
	struct stab keys = {0};
	struct vword *vw = NULL; size_t nvw = 0, capvw = 0;
	/* deterministic: walk spellings in sorted order */
	struct sent **sp = xmalloc((spell.n + 1) * sizeof *sp); size_t nsp = 0;
	for (size_t k = 0; k < spell.cap; k++) if (spell.e[k].s) sp[nsp++] = &spell.e[k];
	qsort(sp, nsp, sizeof *sp, sp_cmp);
	for (size_t k = 0; k < nsp; k++) {
		char f[PT_WORD_MAX * 2 + 8];
		size_t fl = pt_fold_str(sp[k]->s, sp[k]->len, f);
		if (!word_ok(f, fl)) continue;
		struct sent *e = stab_get(&keys, f, fl, 1);
		if (!e->count && !e->aux) {
			if (nvw == capvw) vw = xrealloc(vw, (capvw = capvw ? capvw * 2 : 4096) * sizeof *vw);
			vw[nvw] = (struct vword){ e->s, sp[k]->s, 0, 0, 0 };
			e->aux = (uint32_t)++nvw;   /* index + 1 while collecting */
		}
		struct vword *v = &vw[e->aux - 1];
		v->count += sp[k]->count;
		e->count += sp[k]->count;
		/* the spelling to show: the most used one inside sentences (a capital at a
		 * sentence start says nothing), else the most used one */
		if (sp[k]->mid > v->dmid || (!v->dmid && !sp[k]->mid && sp[k]->count > v->dcount)) {
			v->dmid = sp[k]->mid; v->dcount = sp[k]->count; v->disp = sp[k]->s;
		}
	}
	qsort(vw, nvw, sizeof *vw, vw_count_cmp);
	size_t nv = 0;
	while (nv < nvw && nv < vmax && vw[nv].count >= vmin) nv++;
	qsort(vw, nv, sizeof *vw, vw_key_cmp);
	for (size_t k = 0; k < keys.cap; k++) if (keys.e[k].s) keys.e[k].aux = NONE;
	uint64_t vtotal = 0;
	for (size_t k = 0; k < nv; k++) {
		stab_get(&keys, vw[k].key, strlen(vw[k].key), 0)->aux = (uint32_t)k;
		vtotal += vw[k].count;
	}
	fprintf(stderr, "%zu spellings, %zu words, vocabulary %zu\n", spell.n, nvw, nv);

	/* pass 2: n-grams */
	struct p2 p = { &keys, {0}, {0}, (uint32_t)nv, 0 };
	for (size_t c = 0; c < ncorp; c++) each_line(&cs[c], pass2, &p);
	fprintf(stderr, "%llu tokens, %zu word pairs, %zu triples\n", (unsigned long long)p.ntok, p.bi.n, p.tri.n);

	size_t nr = 0;
	struct row *r = xmalloc((p.bi.n + 1) * sizeof *r);
	for (size_t k = 0; k < p.bi.cap; k++)
		if (p.bi.k[k]) { uint64_t key = p.bi.k[k] - 1; r[nr++] = (struct row){ key >> 21, (uint32_t)(key & 0x1FFFFF), p.bi.c[k] }; }
	free(p.bi.k); free(p.bi.c);
	uint64_t *bctx; uint32_t *bst, *bsucc; size_t nbsucc;
	size_t nbc = prune(r, nr, bik, bimin, 1, &bctx, &bst, &bsucc, &nbsucc);
	free(r);

	/* triples: count the pair totals first so small pairs are dropped early */
	nr = 0;
	r = xmalloc((p.tri.n + 1) * sizeof *r);
	for (size_t k = 0; k < p.tri.cap; k++)
		if (p.tri.k[k]) { uint64_t key = p.tri.k[k] - 1; r[nr++] = (struct row){ key >> 21, (uint32_t)(key & 0x1FFFFF), p.tri.c[k] }; }
	free(p.tri.k); free(p.tri.c);
	uint64_t *tctx; uint32_t *tst, *tsucc; size_t ntsucc;
	size_t ntc = prune(r, nr, trik, trimin, tripair, &tctx, &tst, &tsucc, &ntsucc);
	free(r);

	/* the file */
	FILE *f = fopen(out, "wb");
	if (!f) die("cannot write the output");
	struct gpm_header h = {0};
	memcpy(h.magic, GPM_MAGIC, 4);
	h.version = GPM_VERSION;
	memcpy(h.lang, lang, strlen(lang));
	h.nvocab = (uint32_t)nv;
	put(f, &h, sizeof h);

	/* strings: key, and the spelling when it differs */
	h.off_str = (uint32_t)ftell(f);
	struct gpm_word *gw = xcalloc(nv + 1, sizeof *gw);
	uint32_t so = 0;
	for (size_t k = 0; k < nv; k++) {
		size_t kl = strlen(vw[k].key) + 1;
		gw[k].key = so; put(f, vw[k].key, kl); so += (uint32_t)kl;
		if (strcmp(vw[k].key, vw[k].disp)) {
			size_t dl = strlen(vw[k].disp) + 1;
			gw[k].disp = so; put(f, vw[k].disp, dl); so += (uint32_t)dl;
		} else gw[k].disp = gw[k].key;
		gw[k].q = quant((double)vw[k].count / (double)vtotal);
	}
	h.len_str = so;
	align4(f);
	h.off_vocab = (uint32_t)ftell(f);
	put(f, gw, nv * sizeof *gw);

	h.off_bi = (uint32_t)ftell(f);
	uint32_t *bi = xcalloc(nv + 2, 4);
	for (size_t id = 0, c = 0; id <= nv + 1; id++) {
		while (c < nbc && bctx[c] < id) c++;
		bi[id] = c < nbc ? bst[c] : (uint32_t)nbsucc;
	}
	put(f, bi, (nv + 2) * 4);
	h.off_bisucc = (uint32_t)ftell(f); h.nbisucc = (uint32_t)nbsucc;
	put(f, bsucc, nbsucc * 4);

	h.off_tri = (uint32_t)ftell(f); h.ntri = (uint32_t)ntc;
	for (size_t c = 0; c < ntc; c++) {
		struct gpm_tri t = { (uint32_t)(tctx[c] >> 21), (uint32_t)(tctx[c] & 0x1FFFFF), tst[c] };
		put(f, &t, sizeof t);
	}
	h.off_trisucc = (uint32_t)ftell(f); h.ntrisucc = (uint32_t)ntsucc;
	put(f, tsucc, ntsucc * 4);

	/* likeliest words without context */
	struct row *top = xmalloc((nv + 1) * sizeof *top);
	for (size_t k = 0; k < nv; k++) top[k] = (struct row){ 0, (uint32_t)k, (uint32_t)(vw[k].count > UINT32_MAX ? UINT32_MAX : vw[k].count) };
	qsort(top, nv, sizeof *top, row_cmp);
	h.off_top = (uint32_t)ftell(f); h.ntop = nv < 32 ? (uint32_t)nv : 32;
	for (uint32_t k = 0; k < h.ntop; k++) { uint32_t e = top[k].id << 8 | gw[top[k].id].q; put(f, &e, 4); }

	fseek(f, 0, SEEK_SET);
	put(f, &h, sizeof h);
	if (fclose(f)) die("write failed");
	fprintf(stderr, "%s: %zu words, %zu contexts with %zu next words, %zu pairs with %zu next words\n",
		out, nv, nbc, nbsucc, ntc, ntsucc);
	return 0;
}
