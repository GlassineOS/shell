// SPDX-License-Identifier: GPL-2.0-or-later
// The learned words store (store.h, docs/predictor.md).
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "store.h"
#include "text.h"

#define HEADER "glassine-words"

int store_dir(char *buf, size_t n)
{
	const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
	int r;
	if (x && x[0] == '/') r = snprintf(buf, n, "%s/glassine/predict", x);
	else if (h && h[0] == '/') r = snprintf(buf, n, "%s/.local/share/glassine/predict", h);
	else return -1;
	return r > 0 && (size_t)r < n ? 0 : -1;
}

uint32_t store_today(void) { return (uint32_t)(time(NULL) / 86400); }

int store_valid_lang(const char *l, int star_ok)
{
	if (star_ok && !strcmp(l, "*")) return 1;
	size_t n = strlen(l);
	if (n < 2 || n >= STORE_LANG_MAX) return 0;
	for (size_t i = 0; i < n; i++) {
		char c = l[i];
		if (!((c >= 'a' && c <= 'z') || (i >= 2 && ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))))
			return 0;
	}
	return 1;
}

int store_valid_word(const char *w)
{
	size_t n = strlen(w), pos = 0, ws, wl;
	if (n < 1 || n > PT_WORD_MAX) return 0;
	if (pt_next(w, n, &pos, &ws, &wl, 0) != PT_WORD || ws != 0 || wl != n) return 0;
	return 1;
}

static char *fold_dup(const char *w)
{
	size_t n = strlen(w);
	char *f = malloc(2 * n + 8);
	if (f) pt_fold_str(w, n, f);
	return f;
}

static int cmp(const char *la, const char *ka, const char *lb, const char *kb)
{
	int c = strcmp(la, lb);
	return c ? c : strcmp(ka, kb);
}

/* index of the first entry >= (lang, key) */
static size_t lower(const struct sword *a, size_t n, const char *lang, const char *key)
{
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t m = (lo + hi) / 2;
		if (cmp(a[m].lang, a[m].key, lang, key) < 0) lo = m + 1; else hi = m;
	}
	return lo;
}

static struct sword *insert(struct sword **a, size_t *n, size_t *cap, size_t at)
{
	if (*n == *cap) {
		size_t c = *cap ? *cap * 2 : 64;
		struct sword *p = realloc(*a, c * sizeof *p);
		if (!p) return NULL;
		*a = p; *cap = c;
	}
	memmove(*a + at + 1, *a + at, (*n - at) * sizeof **a);
	(*n)++;
	memset(*a + at, 0, sizeof **a);
	return *a + at;
}

static void drop(struct sword *a, size_t *n, size_t at)
{
	free(a[at].key); free(a[at].word);
	memmove(a + at, a + at + 1, (*n - at - 1) * sizeof *a);
	(*n)--;
}

struct sword *store_find(const struct store *s, const char *lang, const char *key)
{
	size_t i = lower(s->w, s->nw, lang, key);
	return i < s->nw && !cmp(s->w[i].lang, s->w[i].key, lang, key) ? &s->w[i] : NULL;
}

size_t store_prefix(const struct store *s, const char *lang, const char *prefix)
{
	return lower(s->w, s->nw, lang, prefix);
}

int store_removed(const struct store *s, const char *key)
{
	/* a removed word is out in every language, whatever language its line names */
	for (size_t i = 0; i < s->nx; i++) if (!strcmp(s->x[i].key, key)) return 1;
	return 0;
}

static void add_word(struct store *s, const char *lang, const char *word, const char *key, uint32_t n, uint32_t day)
{
	size_t i = lower(s->w, s->nw, lang, key);
	if (i < s->nw && !cmp(s->w[i].lang, s->w[i].key, lang, key)) {
		struct sword *e = &s->w[i];
		e->count = e->count > UINT32_MAX - n ? UINT32_MAX : e->count + n;
		if (day >= e->day) {
			e->day = day;
			/* the spelling as last typed, but a capital only on the first letter (a
			 * sentence start) doesn't replace a lowercase one */
			int cap_only = 0;
			{
				uint32_t a0, b0;
				int la = pt_u8dec((const unsigned char *)e->word, strlen(e->word), &a0);
				int lb = pt_u8dec((const unsigned char *)word, strlen(word), &b0);
				cap_only = !pt_is_upper(a0) && pt_is_upper(b0) && pt_fold(b0) == a0 && !strcmp(e->word + la, word + lb);
			}
			if (!cap_only && strcmp(e->word, word)) { char *d = strdup(word); if (d) { free(e->word); e->word = d; } }
		}
		return;
	}
	char *k = strdup(key), *w = strdup(word);
	struct sword *e = k && w ? insert(&s->w, &s->nw, &s->capw, i) : NULL;
	if (!e) { free(k); free(w); return; }
	snprintf(e->lang, sizeof e->lang, "%s", lang);
	e->key = k; e->word = w; e->count = n; e->day = day;
}

void store_learn(struct store *s, const char *lang, const char *word, uint32_t n, uint32_t day)
{
	char *key = fold_dup(word);
	if (!key) return;
	if (!store_removed(s, key)) add_word(s, lang, word, key, n, day);
	free(key);
}

void store_remove(struct store *s, const char *word)
{
	char *key = fold_dup(word);
	if (!key) return;
	for (size_t i = 0; i < s->nw;)
		if (!strcmp(s->w[i].key, key)) drop(s->w, &s->nw, i);
		else i++;
	if (store_removed(s, key)) { free(key); return; }
	size_t i = lower(s->x, s->nx, "*", key);
	char *w = strdup(word);
	struct sword *e = w ? insert(&s->x, &s->nx, &s->capx, i) : NULL;
	if (!e) { free(w); free(key); return; }
	snprintf(e->lang, sizeof e->lang, "*");
	e->key = key; e->word = w;
}

int store_restore(struct store *s, const char *word)
{
	char *key = fold_dup(word);
	int n = 0;
	if (!key) return 0;
	for (size_t i = 0; i < s->nx;)
		if (!strcmp(s->x[i].key, key)) { drop(s->x, &s->nx, i); n++; }
		else i++;
	free(key);
	return n;
}

void store_erase(struct store *s)
{
	uint64_t e = s->epoch + 1;
	store_free(s);
	s->epoch = e;
}

double store_weight(const struct sword *w, uint32_t today)
{
	/* recency: a use counts half after 90 days without one */
	double age = today > w->day ? (double)(today - w->day) : 0;
	return w->count * exp2(-age / 90.0);
}

static uint32_t trim_today;
static int by_weight(const void *a, const void *b)
{
	double x = store_weight(a, trim_today), y = store_weight(b, trim_today);
	if (x != y) return x > y ? -1 : 1;
	const struct sword *p = a, *q = b;
	return cmp(p->lang, p->key, q->lang, q->key);
}
static int by_key(const void *a, const void *b)
{
	const struct sword *p = a, *q = b;
	return cmp(p->lang, p->key, q->lang, q->key);
}

void store_trim(struct store *s, size_t max, uint32_t today)
{
	if (s->nw <= max) return;
	trim_today = today;
	qsort(s->w, s->nw, sizeof *s->w, by_weight);
	while (s->nw > max) { s->nw--; free(s->w[s->nw].key); free(s->w[s->nw].word); }
	qsort(s->w, s->nw, sizeof *s->w, by_key);
}

void store_free(struct store *s)
{
	for (size_t i = 0; i < s->nw; i++) { free(s->w[i].key); free(s->w[i].word); }
	for (size_t i = 0; i < s->nx; i++) { free(s->x[i].key); free(s->x[i].word); }
	free(s->w); free(s->x);
	memset(s, 0, sizeof *s);
}

int store_load(struct store *s, const char *dir)
{
	char path[4096];
	memset(s, 0, sizeof *s);
	snprintf(path, sizeof path, "%s/words", dir);
	FILE *f = fopen(path, "re");
	if (!f) return errno == ENOENT ? 0 : -1;
	char *line = NULL; size_t cap = 0; ssize_t n;
	int ok = 0;
	if ((n = getline(&line, &cap, f)) > 0) {
		unsigned long long ep;
		int ver;
		char tail;
		if (sscanf(line, HEADER " %d %llu%c", &ver, &ep, &tail) == 3 && ver == 1 && tail == '\n') {
			s->epoch = ep;
			ok = 1;
		}
	}
	while (ok && (n = getline(&line, &cap, f)) > 0) {
		if (line[n - 1] == '\n') line[--n] = 0;
		char *f1 = strtok(line, "\t"), *lang = strtok(NULL, "\t"), *word = strtok(NULL, "\t");
		char *c = strtok(NULL, "\t"), *d = strtok(NULL, "\t");
		if (!f1 || !lang || !word || !store_valid_word(word)) continue;
		if (!strcmp(f1, "w") && c && d && store_valid_lang(lang, 0)) {
			char *key = fold_dup(word);
			unsigned long cn = strtoul(c, NULL, 10), dn = strtoul(d, NULL, 10);
			if (key && cn) add_word(s, lang, word, key, (uint32_t)(cn > UINT32_MAX ? UINT32_MAX : cn), (uint32_t)dn);
			free(key);
		} else if (!strcmp(f1, "x") && store_valid_lang(lang, 1)) {
			/* lines from version 1 may name one language; they count for all */
			char *key = fold_dup(word), *w = strdup(word);
			struct sword *e = key && w && !store_removed(s, key) ? insert(&s->x, &s->nx, &s->capx, lower(s->x, s->nx, lang, key)) : NULL;
			if (!e) { free(key); free(w); continue; }
			snprintf(e->lang, sizeof e->lang, "%s", lang);
			e->key = key; e->word = w;
		}
	}
	free(line);
	fclose(f);
	if (!ok) { store_free(s); return -1; }
	/* a removed word is never kept as learned */
	for (size_t i = 0; i < s->nw;)
		if (store_removed(s, s->w[i].key)) drop(s->w, &s->nw, i); else i++;
	return 0;
}

static int mkdirs(const char *dir)
{
	char p[4096];
	snprintf(p, sizeof p, "%s", dir);
	/* parents as needed; the store's own directory (the last part) is private */
	for (char *q = p + 1; *q; q++)
		if (*q == '/') {
			*q = 0;
			if (mkdir(p, 0700) && errno != EEXIST) return -1;
			*q = '/';
		}
	if (mkdir(p, 0700) && errno != EEXIST) return -1;
	return chmod(p, 0700);
}

int store_lock(const char *dir)
{
	char path[4096];
	if (mkdirs(dir)) return -1;
	snprintf(path, sizeof path, "%s/lock", dir);
	int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (fd < 0) return -1;
	fchmod(fd, 0600);
	if (flock(fd, LOCK_EX)) { close(fd); return -1; }
	return fd;
}

int store_save(const struct store *s, const char *dir)
{
	char path[4096], tmp[4096];
	if (mkdirs(dir)) return -1;
	snprintf(path, sizeof path, "%s/words", dir);
	snprintf(tmp, sizeof tmp, "%s/words.tmp", dir);
	unlink(tmp);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0) return -1;
	if (fchmod(fd, 0600)) { close(fd); unlink(tmp); return -1; }
	FILE *f = fdopen(fd, "w");
	if (!f) { close(fd); unlink(tmp); return -1; }
	fprintf(f, HEADER " 1 %llu\n", (unsigned long long)s->epoch);
	for (size_t i = 0; i < s->nw; i++)
		fprintf(f, "w\t%s\t%s\t%u\t%u\n", s->w[i].lang, s->w[i].word, s->w[i].count, s->w[i].day);
	for (size_t i = 0; i < s->nx; i++)
		fprintf(f, "x\t%s\t%s\n", s->x[i].lang, s->x[i].word);
	int bad = fflush(f) || fsync(fd);
	if (fclose(f) || bad || rename(tmp, path)) { unlink(tmp); return -1; }
	return 0;
}
