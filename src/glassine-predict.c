// SPDX-License-Identifier: GPL-2.0-or-later
// glassine-predict: the default word predictor of the on-screen keyboard (spec 006,
// FR-003 to FR-008a) and the tool for the person's learned words. The contract, the
// files and the command line are in docs/predictor.md; in short:
//
//   glassine-predict                 serve: requests on stdin, replies on stdout, one
//                                    line each (contract version 1)
//   glassine-predict words list|removed [-l LANG] [-s TEXT]
//   glassine-predict words remove|restore [-l LANG] WORD
//   glassine-predict words erase
//   glassine-predict bench LANG TEXTFILE   (host measurements: latency and hit rates)
//
// The base model of a language is /usr/share/glassine/predict/LANG.gpm (or the
// directory in $GLASSINE_PREDICT_MODELS), mapped read-only. The learned words are in
// $XDG_DATA_HOME/glassine/predict/words (mode 0600). Nothing here opens a network
// socket, and nothing typed is ever written to stderr.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "predict/gpm.h"
#include "predict/store.h"
#include "predict/text.h"

#define MAX_LINE 32768          /* bytes of one request line, escaped */
#define CTX_TAIL 512            /* bytes of context the predictor looks at */
#define NCAND 3
#define FLUSH_IDLE_MS 2000      /* learned words are written after this long without a request */
#define FLUSH_MAX_MS 30000      /* or after this long in any case */
#define FLUSH_AFTER_N 64
#define LEARNED_MIN 2           /* uses before a word the base model lacks is suggested */

/* ---------------------------------------------------------------- the base model */

struct model {
	char lang[STORE_LANG_MAX];
	int missing;
	void *map; size_t len;
	const struct gpm_header *h;
	const struct gpm_word *v;
	const char *str;
	const uint32_t *bi, *bisucc, *trisucc, *top;
	const struct gpm_tri *tri;
};

static struct model models[8];
static size_t nmodels;

static int in(size_t off, size_t n, size_t sz, size_t len) { return off % 4 == 0 && off <= len && n <= (len - off) / (sz ? sz : 1); }

static int model_check(struct model *m)
{
	const struct gpm_header *h = m->map;
	size_t L = m->len;
	if (L < sizeof *h || memcmp(h->magic, GPM_MAGIC, 4) || h->version != GPM_VERSION) return -1;
	uint32_t V = h->nvocab;
	if (V == 0 || V >= (1u << 21) - 1) return -1;
	if (!in(h->off_vocab, V, sizeof(struct gpm_word), L) || h->off_str > L || h->len_str > L - h->off_str ||
	    !h->len_str || !in(h->off_bi, (size_t)V + 2, 4, L) || !in(h->off_bisucc, h->nbisucc, 4, L) ||
	    !in(h->off_tri, h->ntri, sizeof(struct gpm_tri), L) || !in(h->off_trisucc, h->ntrisucc, 4, L) ||
	    !in(h->off_top, h->ntop, 4, L))
		return -1;
	m->h = h;
	m->str = (const char *)m->map + h->off_str;
	m->v = (const void *)((const char *)m->map + h->off_vocab);
	m->bi = (const void *)((const char *)m->map + h->off_bi);
	m->bisucc = (const void *)((const char *)m->map + h->off_bisucc);
	m->tri = (const void *)((const char *)m->map + h->off_tri);
	m->trisucc = (const void *)((const char *)m->map + h->off_trisucc);
	m->top = (const void *)((const char *)m->map + h->off_top);
	if (m->str[h->len_str - 1]) return -1;
	for (uint32_t i = 0; i < V; i++) {
		if (m->v[i].key >= h->len_str || m->v[i].disp >= h->len_str) return -1;
		if (i && strcmp(m->str + m->v[i - 1].key, m->str + m->v[i].key) >= 0) return -1;
	}
	for (uint32_t i = 0; i <= V; i++) if (m->bi[i] > m->bi[i + 1]) return -1;
	if (m->bi[V + 1] > h->nbisucc) return -1;
	for (uint32_t i = 0; i < h->ntri; i++) {
		if (m->tri[i].start > h->ntrisucc || m->tri[i].a > V || m->tri[i].b > V) return -1;
		if (i && (m->tri[i].start < m->tri[i - 1].start ||
			  ((uint64_t)m->tri[i - 1].a << 32 | m->tri[i - 1].b) >= ((uint64_t)m->tri[i].a << 32 | m->tri[i].b)))
			return -1;
	}
	for (uint32_t i = 0; i < h->nbisucc; i++) if (GPM_ID(m->bisucc[i]) >= V) return -1;
	for (uint32_t i = 0; i < h->ntrisucc; i++) if (GPM_ID(m->trisucc[i]) >= V) return -1;
	for (uint32_t i = 0; i < h->ntop; i++) if (GPM_ID(m->top[i]) >= V) return -1;
	return 0;
}

static struct model *model_get(const char *lang)
{
	for (size_t i = 0; i < nmodels; i++) if (!strcmp(models[i].lang, lang)) return models[i].missing ? NULL : &models[i];
	if (nmodels == sizeof models / sizeof *models) {          /* forget the oldest */
		if (!models[0].missing) munmap(models[0].map, models[0].len);
		memmove(models, models + 1, sizeof models - sizeof *models);
		nmodels--;
	}
	struct model *m = &models[nmodels++];
	memset(m, 0, sizeof *m);
	snprintf(m->lang, sizeof m->lang, "%s", lang);
	m->missing = 1;
	const char *dir = getenv("GLASSINE_PREDICT_MODELS");
	char path[4096];
	snprintf(path, sizeof path, "%s/%s.gpm", dir && *dir ? dir : "/usr/share/glassine/predict", lang);
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	struct stat st;
	if (fd < 0) return NULL;
	if (fstat(fd, &st) || st.st_size < (off_t)sizeof(struct gpm_header)) { close(fd); return NULL; }
	m->len = (size_t)st.st_size;
	m->map = mmap(NULL, m->len, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (m->map == MAP_FAILED) return NULL;
	if (model_check(m)) {
		fprintf(stderr, "glassine-predict: %s: not a usable model\n", path);
		munmap(m->map, m->len);
		return NULL;
	}
	m->missing = 0;
	return m;
}

static const char *mkey(const struct model *m, uint32_t id) { return m->str + m->v[id].key; }

static int64_t model_find(const struct model *m, const char *key)
{
	size_t lo = 0, hi = m->h->nvocab;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int c = strcmp(mkey(m, (uint32_t)mid), key);
		if (!c) return (int64_t)mid;
		if (c < 0) lo = mid + 1; else hi = mid;
	}
	return -1;
}

/* ids whose key starts with p: [*lo, *hi) */
static void model_range(const struct model *m, const char *p, uint32_t *lo, uint32_t *hi)
{
	size_t pl = strlen(p), a = 0, b = m->h->nvocab;
	while (a < b) { size_t mid = (a + b) / 2; if (strcmp(mkey(m, (uint32_t)mid), p) < 0) a = mid + 1; else b = mid; }
	*lo = (uint32_t)a;
	b = m->h->nvocab;
	while (a < b) { size_t mid = (a + b) / 2; if (strncmp(mkey(m, (uint32_t)mid), p, pl) <= 0) a = mid + 1; else b = mid; }
	*hi = (uint32_t)a;
}

static const struct gpm_tri *model_tri(const struct model *m, uint32_t a, uint32_t b, uint32_t *end)
{
	size_t lo = 0, hi = m->h->ntri;
	uint64_t k = (uint64_t)a << 32 | b;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		uint64_t x = (uint64_t)m->tri[mid].a << 32 | m->tri[mid].b;
		if (x == k) { *end = mid + 1 < m->h->ntri ? m->tri[mid + 1].start : m->h->ntrisucc; return &m->tri[mid]; }
		if (x < k) lo = mid + 1; else hi = mid;
	}
	return NULL;
}

static double qp(uint32_t q) { return exp2(-(double)q / 8.0); }

/* ---------------------------------------------------------------- learned words */

static char store_path[4096];
static int have_store_path;
static struct store learned;            /* the file as last read, with the pending words */
static struct store pending;            /* learned since, not written yet */
static uint64_t pending_epoch;
static struct timespec pending_since;
static struct stat learned_st;
static int learned_loaded, learned_unreadable, no_store;
static double last_request_ms;

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static int same_file(const struct stat *a, const struct stat *b)
{
	return a->st_ino == b->st_ino && a->st_dev == b->st_dev && a->st_size == b->st_size &&
	       a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec;
}

/* re-read the file when someone (the words command, another predictor) changed it */
static void learned_refresh(void)
{
	if (no_store) return;
	if (!have_store_path) { have_store_path = !store_dir(store_path, sizeof store_path); if (!have_store_path) return; }
	char f[4200];
	struct stat st;
	snprintf(f, sizeof f, "%s/words", store_path);
	if (stat(f, &st)) memset(&st, 0, sizeof st);
	if (learned_loaded && same_file(&st, &learned_st)) return;
	struct store s;
	if (store_load(&s, store_path)) {
		if (!learned_unreadable) fprintf(stderr, "glassine-predict: the learned words file can't be read; not learning\n");
		learned_unreadable = 1;
		store_free(&learned);
	} else {
		learned_unreadable = 0;
		store_free(&learned);
		learned = s;
		if (learned.epoch != pending_epoch) { store_free(&pending); pending_epoch = learned.epoch; }
		/* what was learned since counts at once, written or not */
		for (size_t i = 0; i < pending.nw; i++)
			store_learn(&learned, pending.w[i].lang, pending.w[i].word, pending.w[i].count, pending.w[i].day);
	}
	learned_st = st;
	learned_loaded = 1;
}

/* read-modify-write under the lock; op may be NULL (just the pending words) */
static int learned_write(void (*op)(struct store *, const char *, const char *), const char *lang, const char *word)
{
	if (!have_store_path) return -1;
	int lk = store_lock(store_path);
	if (lk < 0) return -1;
	struct store s;
	int r = -1;
	if (!store_load(&s, store_path)) {
		if (s.epoch == pending_epoch)
			for (size_t i = 0; i < pending.nw; i++)
				store_learn(&s, pending.w[i].lang, pending.w[i].word, pending.w[i].count, pending.w[i].day);
		if (op) op(&s, lang, word);
		store_trim(&s, STORE_MAX_WORDS, store_today());
		r = store_save(&s, store_path);
		store_free(&s);
	}
	close(lk);
	store_free(&pending);
	learned_loaded = 0;
	learned_refresh();
	return r;
}

static void flush_pending(void) { if (pending.nw) learned_write(NULL, NULL, NULL); }

static void op_remove(struct store *s, const char *l, const char *w) { store_remove(s, l, w); }
static void op_restore(struct store *s, const char *l, const char *w) { store_restore(s, l, w); }

/* $XDG_CONFIG_HOME/glassine/predict.conf: learning=off turns learning off */
static int learning_on(void)
{
	const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
	char p[4096];
	if (x && x[0] == '/') snprintf(p, sizeof p, "%s/glassine/predict.conf", x);
	else if (h && h[0] == '/') snprintf(p, sizeof p, "%s/.config/glassine/predict.conf", h);
	else return 1;
	FILE *f = fopen(p, "re");
	if (!f) return 1;
	char line[256];
	int on = 1;
	while (fgets(line, sizeof line, f)) {
		char *s = line;
		while (*s == ' ' || *s == '\t') s++;
		if (!strncmp(s, "learning", 8)) {
			s += 8;
			while (*s == ' ' || *s == '\t') s++;
			if (*s++ != '=') continue;
			while (*s == ' ' || *s == '\t') s++;
			on = !(!strncmp(s, "off", 3) || !strncmp(s, "false", 5) || *s == '0');
		}
	}
	fclose(f);
	return on;
}

static double learned_total(const struct store *s, const char *lang, uint32_t today)
{
	double t = 0;
	for (size_t i = store_prefix(s, lang, ""); i < s->nw && !strcmp(s->w[i].lang, lang); i++) t += store_weight(&s->w[i], today);
	return t;
}

static int is_removed(const char *lang, const char *key) { return store_removed(&learned, lang, key); }

/* ---------------------------------------------------------------- fields */

enum { F_SUGGEST = 1, F_LEARN = 2, F_CAPS = 4 };

/* text-input-v3 purpose[,hint...] -> flags, or -1. Private (password, pin,
 * sensitive_data, hidden_text): 0. Unknown names never learn. */
static int field_flags(const char *field)
{
	static const char *const purposes[] = { "normal", "alpha", "name", "terminal", "digits", "number", "phone",
		"url", "email", "date", "time", "datetime", "password", "pin" };
	static const char *const hints[] = { "none", "completion", "spellcheck", "auto_capitalization", "lowercase",
		"uppercase", "titlecase", "latin", "multiline", "hidden_text", "sensitive_data" };
	char buf[256];
	if (!*field || strlen(field) >= sizeof buf) return -1;
	strcpy(buf, field);
	char *save, *p = strtok_r(buf, ",", &save);
	if (!p) return -1;
	int known = 0, flags = 0, priv = 0;
	for (size_t i = 0; i < sizeof purposes / sizeof *purposes; i++)
		if (!strcmp(p, purposes[i])) {
			known = 1;
			if (i <= 2) flags = F_SUGGEST | F_LEARN;        /* normal, alpha, name */
			else if (i == 3) flags = F_SUGGEST;              /* terminal: no learning */
			else if (i >= 12) priv = 1;                      /* password, pin */
		}
	if (!known) { for (const char *c = p; *c; c++) if (!((*c >= 'a' && *c <= 'z') || *c == '_')) return -1; flags = F_SUGGEST; }
	while ((p = strtok_r(NULL, ",", &save))) {
		size_t i, n = sizeof hints / sizeof *hints;
		for (i = 0; i < n && strcmp(p, hints[i]); i++) ;
		if (i == n) {
			for (const char *c = p; *c; c++) if (!((*c >= 'a' && *c <= 'z') || *c == '_')) return -1;
			flags &= ~F_LEARN;
		} else if (i >= 9) priv = 1;
		else if (i == 3) flags |= F_CAPS;
	}
	return priv ? 0 : flags;
}

/* ---------------------------------------------------------------- prediction */

struct cand { int64_t id; const struct sword *lw; double score; char key[PT_WORD_MAX * 2 + 8]; };

static struct cand *cand_add(struct cand *c, int *n, int max, int64_t id, const struct sword *lw)
{
	for (int i = 0; i < *n; i++)
		if ((id >= 0 && c[i].id == id) || (id < 0 && lw && c[i].id < 0 && c[i].lw && !strcmp(c[i].lw->key, lw->key))) {
			if (lw && !c[i].lw) c[i].lw = lw;
			return &c[i];
		}
	if (*n == max) return NULL;
	c[*n] = (struct cand){ id, lw, 0, {0} };
	return &c[(*n)++];
}

static int cand_cmp(const void *a, const void *b)
{
	const struct cand *x = a, *y = b;
	if (x->score != y->score) return x->score > y->score ? -1 : 1;
	return strcmp(x->key, y->key);
}

static size_t put_case(const char *w, int mode, char *out)
{
	/* mode 0: as is, 1: first letter up, 2: all up */
	size_t n = strlen(w), i = 0, o = 0;
	while (i < n) {
		uint32_t c;
		i += pt_u8dec((const unsigned char *)w + i, n - i, &c);
		if (mode == 2 || (mode == 1 && o == 0)) c = pt_upper(c);
		o += pt_u8enc(c, out + o);
	}
	out[o] = 0;
	return o;
}

/* before: the text before the word being typed (any length), word: the word so far.
 * Fills out[] with up to NCAND words, best first; returns how many. */
static int predict(const char *lang, int flags, const char *before, size_t blen, const char *word, size_t wlen,
		   char out[NCAND][PT_WORD_MAX * 2 + 8])
{
	if (!(flags & F_SUGGEST) || wlen > PT_WORD_MAX) return 0;
	char text[CTX_TAIL + PT_WORD_MAX + 8];
	size_t start = blen > CTX_TAIL ? blen - CTX_TAIL : 0, tl = 0;
	int cut = start > 0;
	if (cut) {      /* start after a space, never inside a word or a character */
		while (start < blen && before[start] != ' ' && before[start] != '\n') start++;
	}
	memcpy(text, before + start, blen - start); tl = blen - start;
	memcpy(text + tl, word, wlen); tl += wlen;

	/* the tokens: the last word if it touches the end is the partial word */
	size_t pos = 0, ws, wl, pws = 0, pwl = 0;
	int kinds[3] = { PT_END, PT_END, PT_END }; size_t tws[3], twl[3]; int nt = 0;
	int k;
	while ((k = pt_next(text, tl, &pos, &ws, &wl, 1)) != PT_END) {
		kinds[0] = kinds[1]; tws[0] = tws[1]; twl[0] = twl[1];
		kinds[1] = kinds[2]; tws[1] = tws[2]; twl[1] = twl[2];
		kinds[2] = k; tws[2] = ws; twl[2] = wl;
		nt++;
	}
	int partial = nt && kinds[2] == PT_WORD && tws[2] + twl[2] == tl;
	if (!partial && wlen) return 0;          /* the word being typed isn't a word */
	if (partial) { pws = tws[2]; pwl = twl[2]; }
	/* context: the two tokens before the partial word */
	int ck[2]; size_t cws[2], cwl[2];
	int have = partial ? nt - 1 : nt;
	for (int j = 0; j < 2; j++) {
		int idx = partial ? 1 - j : 2 - j;   /* j=0: the word right before */
		if (have - j <= 0) { ck[j] = (cut ? PT_BREAK : PT_SENT); cws[j] = cwl[j] = 0; }
		else { ck[j] = kinds[idx]; cws[j] = tws[idx]; cwl[j] = twl[idx]; }
	}

	struct model *m = model_get(lang);
	learned_refresh();
	const struct store *ls = &learned;
	uint32_t today = store_today();

	char fp[PT_WORD_MAX * 2 + 8];
	size_t fpl = partial ? pt_fold_str(text + pws, pwl, fp) : (fp[0] = 0, 0);

	int64_t h[2] = { -1, -1 };
	if (m)
		for (int j = 0; j < 2; j++) {
			if (ck[j] == PT_SENT) h[j] = m->h->nvocab;
			else if (ck[j] == PT_WORD) {
				char f[PT_WORD_MAX * 2 + 8];
				pt_fold_str(text + cws[j], cwl[j], f);
				h[j] = model_find(m, f);
			}
			if (h[j] < 0) break;
		}
	if (h[0] < 0) h[1] = -1;

	struct cand c[96];
	int nc = 0;
	uint32_t lo = 0, hi = 0;
	const uint32_t *bs = NULL, *be = NULL, *ts = NULL, *te = NULL;
	if (m) {
		if (partial) model_range(m, fp, &lo, &hi); else { lo = 0; hi = m->h->nvocab; }
		if (h[0] >= 0) { bs = m->bisucc + m->bi[h[0]]; be = m->bisucc + m->bi[h[0] + 1]; }
		uint32_t tend;
		const struct gpm_tri *t = h[1] >= 0 ? model_tri(m, (uint32_t)h[1], (uint32_t)h[0], &tend) : NULL;
		if (t) { ts = m->trisucc + t->start; te = m->trisucc + tend; }
		for (const uint32_t *e = ts; e && e < te; e++) if (GPM_ID(*e) >= lo && GPM_ID(*e) < hi) cand_add(c, &nc, 40, GPM_ID(*e), NULL);
		for (const uint32_t *e = bs; e && e < be; e++) if (GPM_ID(*e) >= lo && GPM_ID(*e) < hi) cand_add(c, &nc, 60, GPM_ID(*e), NULL);
		if (partial) {
			/* the likeliest words with this beginning */
			uint32_t best[8]; int nb = 0;
			for (uint32_t id = lo; id < hi; id++) {
				uint8_t q = m->v[id].q;
				if (nb == 8 && q >= m->v[best[7]].q) continue;
				int at = nb < 8 ? nb++ : 7;
				while (at > 0 && m->v[best[at - 1]].q > q) { best[at] = best[at - 1]; at--; }
				best[at] = id;
			}
			for (int i = 0; i < nb; i++) cand_add(c, &nc, 72, best[i], NULL);
		} else
			for (uint32_t i = 0; i < m->h->ntop; i++) cand_add(c, &nc, 72, GPM_ID(m->top[i]), NULL);
	}
	/* learned words with this beginning (only while a word is being typed) */
	double ltot = learned_total(ls, lang, today);
	if (partial)
		for (size_t i = store_prefix(ls, lang, fp); i < ls->nw && !strcmp(ls->w[i].lang, lang) &&
			     !strncmp(ls->w[i].key, fp, fpl); i++) {
			int64_t id = m ? model_find(m, ls->w[i].key) : -1;
			if (id < 0 && ls->w[i].count < LEARNED_MIN) continue;
			cand_add(c, &nc, 96, id, &ls->w[i]);
		}
	/* learned use of the base model's candidates */
	for (int i = 0; i < nc; i++)
		if (c[i].id >= 0 && !c[i].lw) c[i].lw = store_find(ls, lang, mkey(m, (uint32_t)c[i].id));

	double l3 = 0.55, l2 = 0.30, l1 = 0.15;
	if (!ts) { l3 = 0; if (bs) { l2 = 0.8; l1 = 0.2; } else { l2 = 0; l1 = 1; } }
	for (int i = 0; i < nc; i++) {
		double p = 0;
		if (c[i].id >= 0) {
			uint32_t id = (uint32_t)c[i].id;
			double pt = 0, pb = 0;
			for (const uint32_t *e = ts; e && e < te; e++) if (GPM_ID(*e) == id) { pt = qp(GPM_Q(*e)); break; }
			for (const uint32_t *e = bs; e && e < be; e++) if (GPM_ID(*e) == id) { pb = qp(GPM_Q(*e)); break; }
			p = l3 * pt + l2 * pb + l1 * qp(m->v[id].q);
			snprintf(c[i].key, sizeof c[i].key, "%s", mkey(m, id));
		} else snprintf(c[i].key, sizeof c[i].key, "%s", c[i].lw->key);
		if (c[i].lw) p += 0.5 * store_weight(c[i].lw, today) / (ltot + 20.0);
		c[i].score = is_removed(lang, c[i].key) ? -1 : p;
	}
	/* best NCAND, ties by key */
	qsort(c, (size_t)nc, sizeof *c, cand_cmp);
	int n = 0;
	while (n < nc && n < NCAND && c[n].score >= 0) n++;
	/* the case to show */
	int mode = 0;
	if (partial) {
		uint32_t c0; int letters = 0, uppers = 0;
		pt_u8dec((const unsigned char *)text + pws, pwl, &c0);
		for (size_t i = 0; i < pwl;) {
			uint32_t d; i += pt_u8dec((const unsigned char *)text + pws + i, pwl - i, &d);
			if (pt_is_letter(d)) { letters++; uppers += pt_is_upper(d); }
		}
		if (pt_is_upper(c0)) mode = letters >= 2 && uppers == letters ? 2 : 1;
	} else if ((flags & F_CAPS) && ck[0] == PT_SENT) mode = 1;
	for (int i = 0; i < n; i++) {
		const struct cand *x = &c[i];
		const char *w = x->lw ? x->lw->word : m->str + m->v[x->id].disp;
		put_case(w, mode, out[i]);
	}
	return n;
}

/* ---------------------------------------------------------------- the contract */

/* undo the escapes (\\ \t \n \r) in place; -1 on another escape or a NUL */
static int unescape(char *s, size_t *len)
{
	char *o = s;
	for (char *i = s; *i; i++) {
		if (*i != '\\') { *o++ = *i; continue; }
		switch (*++i) {
		case '\\': *o++ = '\\'; break;
		case 't': *o++ = '\t'; break;
		case 'n': *o++ = '\n'; break;
		case 'r': *o++ = '\r'; break;
		default: return -1;
		}
	}
	*o = 0;
	*len = (size_t)(o - s);
	return 0;
}

static void out_field(const char *s)
{
	putchar('\t');
	for (; *s; s++)
		switch (*s) {
		case '\\': fputs("\\\\", stdout); break;
		case '\t': fputs("\\t", stdout); break;
		case '\n': fputs("\\n", stdout); break;
		case '\r': fputs("\\r", stdout); break;
		default: putchar(*s);
		}
}

static void reply(const char *verb, const char *id, const char *a, const char *b)
{
	fputs(verb, stdout);
	out_field(id);
	if (a) out_field(a);
	if (b) out_field(b);
	putchar('\n');
}

static int valid_id(const char *id)
{
	size_t n = strlen(id);
	if (!n || n > 32) return 0;
	for (; *id; id++)
		if (!((*id >= 'a' && *id <= 'z') || (*id >= 'A' && *id <= 'Z') || (*id >= '0' && *id <= '9') ||
		      *id == '.' || *id == '_' || *id == '-'))
			return 0;
	return 1;
}

#define MAXF 8
struct req { char *f[MAXF]; size_t len[MAXF]; int nf; };

/* split a line at tabs; 0, or an error reason */
static const char *split(char *line, struct req *r)
{
	r->nf = 0;
	if (!*line) return "bad-line";
	for (char *p = line;;) {
		if (r->nf == MAXF) return "bad-line";
		char *t = strchr(p, '\t');
		r->f[r->nf++] = p;
		if (!t) break;
		*t = 0;
		p = t + 1;
	}
	return NULL;
}

static int is_verb(const struct req *r, const char *v) { return r->nf > 0 && !strcmp(r->f[0], v); }

static double pending_age_ms(void)
{
	return now_ms() - (pending_since.tv_sec * 1000.0 + pending_since.tv_nsec / 1e6);
}

static void handle(struct req *r, int superseded)
{
	const char *verb = r->f[0], *id = r->nf > 1 ? r->f[1] : "-";
	static const struct { const char *verb; int nf; } verbs[] = {
		{ "hello", 3 }, { "predict", 6 }, { "learn", 5 }, { "remove", 4 }, { "restore", 4 } };
	size_t v;
	for (v = 0; v < sizeof verbs / sizeof *verbs && strcmp(verb, verbs[v].verb); v++) ;
	if (!valid_id(id)) { reply("error", "-", "bad-id", NULL); return; }
	if (v == sizeof verbs / sizeof *verbs) { reply("error", id, "unknown-verb", NULL); return; }
	if (r->nf != verbs[v].nf) { reply("error", id, "bad-line", NULL); return; }
	for (int i = 2; i < r->nf; i++)
		if (unescape(r->f[i], &r->len[i]) || strlen(r->f[i]) != r->len[i]) { reply("error", id, "bad-escape", NULL); return; }

	if (v == 0) {           /* hello ID VERSION */
		reply("hello", id, "1", NULL);
		return;
	}
	const char *lang = r->f[2];
	if (!store_valid_lang(lang, v >= 3)) { reply("error", id, "bad-lang", NULL); return; }

	if (v == 1) {           /* predict ID LANG FIELD BEFORE WORD */
		int fl = field_flags(r->f[3]);
		if (fl < 0) { reply("error", id, "bad-field", NULL); return; }
		if (superseded) { reply("drop", id, NULL, NULL); return; }
		char out[NCAND][PT_WORD_MAX * 2 + 8];
		int n = fl ? predict(lang, fl, r->f[4], r->len[4], r->f[5], r->len[5], out) : 0;
		fputs("predict", stdout);
		out_field(id);
		for (int i = 0; i < n; i++) out_field(out[i]);
		putchar('\n');
		return;
	}
	if (v == 2) {           /* learn ID LANG FIELD WORD */
		int fl = field_flags(r->f[3]);
		if (fl < 0) { reply("error", id, "bad-field", NULL); return; }
		const char *w = r->f[4];
		if (!(fl & F_LEARN) || r->len[4] < 2 || !store_valid_word(w) || !learning_on()) { reply("learn", id, "skip", NULL); return; }
		learned_refresh();
		char key[PT_WORD_MAX * 2 + 8];
		pt_fold_str(w, strlen(w), key);
		if (!have_store_path || learned_unreadable || store_removed(&learned, lang, key)) { reply("learn", id, "skip", NULL); return; }
		if (!pending.nw) clock_gettime(CLOCK_MONOTONIC, &pending_since);
		store_learn(&pending, lang, w, 1, store_today());
		store_learn(&learned, lang, w, 1, store_today());
		reply("learn", id, "ok", NULL);
		return;
	}
	/* remove / restore ID LANG WORD: written at once */
	const char *w = r->f[3];
	if (!store_valid_word(w)) { reply("error", id, "bad-word", NULL); return; }
	learned_refresh();
	if (!have_store_path || learned_unreadable) { reply("error", id, "store", NULL); return; }
	if (v == 3) {
		if (learned_write(op_remove, lang, w)) { reply("error", id, "store", NULL); return; }
		reply("remove", id, "ok", NULL);
	} else {
		char key[PT_WORD_MAX * 2 + 8];
		pt_fold_str(w, strlen(w), key);
		int had = 0;
		for (size_t i = 0; i < learned.nx; i++)
			if (!strcmp(learned.x[i].key, key) && (!strcmp(lang, "*") || !strcmp(learned.x[i].lang, lang))) had = 1;
		if (had && learned_write(op_restore, lang, w)) { reply("error", id, "store", NULL); return; }
		reply("restore", id, had ? "ok" : "none", NULL);
	}
}

static volatile sig_atomic_t stop;
static void on_signal(int s) { (void)s; stop = 1; }

static int serve(void)
{
	struct sigaction sa = { 0 };
	sa.sa_handler = on_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	static char buf[MAX_LINE * 8 + 1];
	size_t len = 0;
	int discarding = 0, eof = 0;
	while (!stop && !eof) {
		int timeout = -1;
		if (pending.nw) {
			double idle = FLUSH_IDLE_MS - (now_ms() - last_request_ms), max = FLUSH_MAX_MS - pending_age_ms();
			double left = idle < max ? idle : max;
			timeout = left > 0 ? (int)left + 1 : 0;
		}
		struct pollfd pfd = { 0, POLLIN, 0 };
		int pr = poll(&pfd, 1, timeout);
		if (pr < 0) { if (errno == EINTR) continue; break; }
		if (pr == 0) { flush_pending(); continue; }
		/* everything that is already there, so older predictions can be dropped */
		do {
			ssize_t n = read(0, buf + len, sizeof buf - 1 - len);
			if (n < 0) { if (errno == EINTR) { if (stop) break; continue; } eof = 1; break; }
			if (n == 0) { eof = 1; break; }
			len += (size_t)n;
		} while (len < sizeof buf - 1 && poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLIN));

		last_request_ms = now_ms();
		/* the complete lines */
		char *lines[4096]; int nl = 0;
		size_t at = 0;
		for (size_t i = 0; i < len; i++) {
			if (buf[i] != '\n') continue;
			buf[i] = 0;
			if (discarding) discarding = 0;
			else if (i - at > MAX_LINE) reply("error", "-", "too-long", NULL);
			else if (memchr(buf + at, 0, i - at)) reply("error", "-", "bad-line", NULL);
			else if (nl < 4096) lines[nl++] = buf + at;
			else reply("error", "-", "busy", NULL);
			at = i + 1;
		}
		struct req rs[64];
		int last_predict = -1;
		/* the newest prediction is the one answered */
		for (int i = 0; i < nl; i++) if (!strncmp(lines[i], "predict\t", 8)) last_predict = i;
		for (int i = 0; i < nl; i++) {
			struct req *r = &rs[i % 64];
			const char *err = split(lines[i], r);
			if (err) { reply("error", "-", err, NULL); continue; }
			handle(r, is_verb(r, "predict") && i != last_predict);
		}
		if (pending.nw >= FLUSH_AFTER_N) flush_pending();
		/* keep an incomplete line; one longer than allowed is dropped up to its end */
		memmove(buf, buf + at, len - at);
		len -= at;
		if (len > MAX_LINE) {
			if (!discarding) reply("error", "-", "too-long", NULL);
			discarding = 1;
			len = 0;
		}
		if (fflush(stdout) == EOF) break;
	}
	if (eof && len && !discarding) reply("error", "-", "bad-line", NULL);
	fflush(stdout);
	flush_pending();
	return 0;
}

/* ---------------------------------------------------------------- the words command */

static int usage(void)
{
	fputs("usage: glassine-predict\n"
	      "       glassine-predict words list|removed [-l LANG] [-s TEXT]\n"
	      "       glassine-predict words remove|restore [-l LANG] WORD\n"
	      "       glassine-predict words erase\n"
	      "       glassine-predict bench LANG TEXTFILE\n", stderr);
	return 2;
}

static const struct store *sort_store;
static uint32_t sort_today;
static int by_use(const void *a, const void *b)
{
	const struct sword *x = &sort_store->w[*(const size_t *)a], *y = &sort_store->w[*(const size_t *)b];
	if (x->count != y->count) return x->count > y->count ? -1 : 1;
	if (x->day != y->day) return x->day > y->day ? -1 : 1;
	int c = strcmp(x->key, y->key);
	return c ? c : strcmp(x->lang, y->lang);
}

static int words(int argc, char **argv)
{
	if (argc < 1) return usage();
	const char *cmd = argv[0], *lang = NULL, *search = NULL, *word = NULL;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-l") && i + 1 < argc) lang = argv[++i];
		else if (!strcmp(argv[i], "-s") && i + 1 < argc) search = argv[++i];
		else if (argv[i][0] == '-' && argv[i][1]) return usage();
		else if (!word) word = argv[i];
		else return usage();
	}
	int listing = !strcmp(cmd, "list") || !strcmp(cmd, "removed");
	int changing = !strcmp(cmd, "remove") || !strcmp(cmd, "restore");
	if (!listing && !changing && strcmp(cmd, "erase")) return usage();
	if ((changing != (word != NULL)) || (search && !listing) || (lang && !strcmp(cmd, "erase"))) return usage();
	if (lang && !store_valid_lang(lang, changing)) { fputs("glassine-predict: bad language\n", stderr); return 2; }
	if (word && !store_valid_word(word)) { fputs("glassine-predict: not a word\n", stderr); return 2; }
	char dir[4096];
	if (store_dir(dir, sizeof dir)) { fputs("glassine-predict: no home directory\n", stderr); return 1; }
	struct store s;
	if (listing) {
		if (store_load(&s, dir)) { fputs("glassine-predict: the learned words file can't be read\n", stderr); return 1; }
		char fs[PT_WORD_MAX * 4 + 8] = "";
		if (search) { if (strlen(search) > PT_WORD_MAX * 2) return usage(); pt_fold_str(search, strlen(search), fs); }
		if (!strcmp(cmd, "list")) {
			size_t *ix = malloc((s.nw + 1) * sizeof *ix), n = 0;
			if (!ix) return 1;
			for (size_t i = 0; i < s.nw; i++)
				if ((!lang || !strcmp(s.w[i].lang, lang)) && (!*fs || strstr(s.w[i].key, fs))) ix[n++] = i;
			sort_store = &s; sort_today = store_today();
			qsort(ix, n, sizeof *ix, by_use);
			for (size_t i = 0; i < n; i++)
				printf("%s\t%s\t%u\t%u\n", s.w[ix[i]].lang, s.w[ix[i]].word, s.w[ix[i]].count, s.w[ix[i]].day);
			free(ix);
		} else
			for (size_t i = 0; i < s.nx; i++)
				if ((!lang || !strcmp(s.x[i].lang, lang) || !strcmp(s.x[i].lang, "*")) && (!*fs || strstr(s.x[i].key, fs)))
					printf("%s\t%s\n", s.x[i].lang, s.x[i].word);
		store_free(&s);
		return fflush(stdout) ? 1 : 0;
	}
	int lk = store_lock(dir);
	if (lk < 0) { fputs("glassine-predict: can't lock the learned words\n", stderr); return 1; }
	if (store_load(&s, dir)) { close(lk); fputs("glassine-predict: the learned words file can't be read\n", stderr); return 1; }
	if (!strcmp(cmd, "remove")) store_remove(&s, lang ? lang : "*", word);
	else if (!strcmp(cmd, "restore")) store_restore(&s, lang ? lang : "*", word);
	else store_erase(&s);
	int r = store_save(&s, dir);
	store_free(&s);
	close(lk);
	if (r) { fputs("glassine-predict: can't write the learned words\n", stderr); return 1; }
	return 0;
}

/* ---------------------------------------------------------------- measurements */

static int dcmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

/* Types every line of TEXTFILE letter by letter: the time of each prediction, and how
 * often the next word is among the three (before its first letter, after one, after two). */
static int bench(const char *lang, const char *file)
{
	FILE *f = fopen(file, "r");
	no_store = 1;           /* the base model alone */
	if (!f || !model_get(lang)) { fputs("glassine-predict: bench needs the text and the model\n", stderr); return 1; }
	size_t cap = 1 << 20, n = 0;
	double *t = malloc(cap * sizeof *t);
	long hit[3] = { 0 }, tries[3] = { 0 }, saved = 0, typed = 0;
	char *line = NULL; size_t lc = 0; ssize_t ll;
	char out[NCAND][PT_WORD_MAX * 2 + 8];
	int lines = 0;
	while ((ll = getline(&line, &lc, f)) > 0 && lines < 20000) {
		lines++;
		if (line[ll - 1] == '\n') line[--ll] = 0;
		size_t pos = 0, ws, wl;
		int k;
		while ((k = pt_next(line, (size_t)ll, &pos, &ws, &wl, 0)) != PT_END) {
			if (k != PT_WORD) continue;
			char fw[PT_WORD_MAX * 2 + 8];
			pt_fold_str(line + ws, wl, fw);
			int done = 0;
			typed += (long)wl;
			for (size_t p = 0; p <= wl && p < 3 + wl; p++) {
				if (p && (line[ws + p - 1] & 0xC0) == 0x80) continue;
				double t0 = now_ms();
				int c = predict(lang, F_SUGGEST, line, ws, line + ws, p, out);
				double dt = now_ms() - t0;
				if (n == cap) t = realloc(t, (cap *= 2) * sizeof *t);
				t[n++] = dt;
				int ok = 0;
				for (int i = 0; i < c; i++) {
					char fo[PT_WORD_MAX * 2 + 8];
					pt_fold_str(out[i], strlen(out[i]), fo);
					if (!strcmp(fo, fw)) ok = 1;
				}
				if (p < 3) { tries[p]++; hit[p] += ok; }
				if (ok && !done) { done = 1; saved += (long)(wl - p); }
			}
		}
	}
	fclose(f);
	/* the worst case: a 4 KB context and a one-letter word */
	char big[4096];
	for (size_t i = 0; i < sizeof big; i++) big[i] = "the quick brown fox jumps over the lazy dog "[i % 44];
	double worst = 0;
	for (int i = 0; i < 1000; i++) {
		double t0 = now_ms();
		predict(lang, F_SUGGEST, big, sizeof big, "t", 1, out);
		double dt = now_ms() - t0;
		if (dt > worst) worst = dt;
	}
	qsort(t, n, sizeof *t, dcmp);
	printf("predictions %zu: p50 %.3f ms, p99 %.3f ms, max %.3f ms; 4 KB context, one letter: max %.3f ms\n",
	       n, n ? t[n / 2] : 0, n ? t[n * 99 / 100] : 0, n ? t[n - 1] : 0, worst);
	printf("next word in the three: %.1f%% before a letter, %.1f%% after one, %.1f%% after two; letters saved %.1f%%\n",
	       tries[0] ? 100.0 * hit[0] / tries[0] : 0, tries[1] ? 100.0 * hit[1] / tries[1] : 0,
	       tries[2] ? 100.0 * hit[2] / tries[2] : 0, typed ? 100.0 * saved / typed : 0);
	free(t); free(line);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 1) return serve();
	if (!strcmp(argv[1], "words")) return words(argc - 2, argv + 2);
	if (!strcmp(argv[1], "bench") && argc == 4) return bench(argv[2], argv[3]);
	return usage();
}
