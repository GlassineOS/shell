// SPDX-License-Identifier: GPL-2.0-or-later
// The learned words store of glassine-predict (format version 1, docs/predictor.md):
// the person's words with how often and when they were last used, and the words they
// removed (never suggested, never learned again). Sorted by language, then folded word.
#ifndef GLASSINE_PREDICT_STORE_H
#define GLASSINE_PREDICT_STORE_H

#include <stddef.h>
#include <stdint.h>

#define STORE_LANG_MAX 16
#define STORE_MAX_WORDS 20000   /* per store; the least used go first */

struct sword {
	char lang[STORE_LANG_MAX];
	char *key;              /* folded */
	char *word;             /* as last typed (learned) or as removed */
	uint32_t count, day;    /* day: days since 1970-01-01 (learned words only) */
};

struct store {
	uint64_t epoch;         /* bumped by erase */
	struct sword *w; size_t nw, capw;       /* learned */
	struct sword *x; size_t nx, capx;       /* removed; lang "*" = every language */
};

/* The store's directory: $XDG_DATA_HOME/glassine/predict (absolute XDG_DATA_HOME),
 * else $HOME/.local/share/glassine/predict. Returns 0, or -1 with no usable home. */
int store_dir(char *buf, size_t n);
/* 0 and the contents (empty when there is no file); -1 for a file this version can't
 * read (it must not be overwritten). */
int store_load(struct store *s, const char *dir);
/* The lock for read-modify-write (creates the directories, mode 0700): a descriptor to
 * close, or -1. */
int store_lock(const char *dir);
/* Atomic replace, mode 0600 whatever the umask. 0 or -1. */
int store_save(const struct store *s, const char *dir);
void store_free(struct store *s);

int store_valid_lang(const char *lang, int star_ok);
/* A word the store accepts: up to 48 bytes, letters with inner apostrophes. */
int store_valid_word(const char *w);

struct sword *store_find(const struct store *s, const char *lang, const char *key);
/* first learned word of lang whose key starts with prefix (then walk while it matches) */
size_t store_prefix(const struct store *s, const char *lang, const char *prefix);
int store_removed(const struct store *s, const char *lang, const char *key);
void store_learn(struct store *s, const char *lang, const char *word, uint32_t n, uint32_t day);
/* removes the learned word and keeps it out; lang "*" = every language */
void store_remove(struct store *s, const char *lang, const char *word);
/* puts a removed word back (lang "*": every entry of it); returns how many */
int store_restore(struct store *s, const char *lang, const char *word);
/* forgets every learned word and the removed list */
void store_erase(struct store *s);
/* drops the least used words beyond max */
void store_trim(struct store *s, size_t max, uint32_t today);
double store_weight(const struct sword *w, uint32_t today);
uint32_t store_today(void);

#endif
