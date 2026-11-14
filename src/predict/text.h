// SPDX-License-Identifier: GPL-2.0-or-later
// Text rules shared by glassine-predict and its model builder: UTF-8, what a word is,
// case folding and the tokenizer. Both sides must cut and fold text the same way, so
// this is the only copy. No locale: the rules are fixed here.
#ifndef GLASSINE_PREDICT_TEXT_H
#define GLASSINE_PREDICT_TEXT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PT_WORD_MAX 48          /* bytes; longer runs are not words */

/* One code point from s[0..n); returns its length (1 for a bad byte, read as U+FFFD). */
static inline int pt_u8dec(const unsigned char *s, size_t n, uint32_t *cp)
{
	unsigned c = s[0];
	int len; uint32_t v;
	if (c < 0x80) { *cp = c; return 1; }
	if (c >= 0xC2 && c <= 0xDF) { len = 2; v = c & 0x1F; }
	else if (c >= 0xE0 && c <= 0xEF) { len = 3; v = c & 0x0F; }
	else if (c >= 0xF0 && c <= 0xF4) { len = 4; v = c & 0x07; }
	else { *cp = 0xFFFD; return 1; }
	if ((size_t)len > n) { *cp = 0xFFFD; return 1; }
	for (int i = 1; i < len; i++) {
		if ((s[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
		v = (v << 6) | (s[i] & 0x3F);
	}
	if ((len == 3 && v < 0x800) || (len == 4 && (v < 0x10000 || v > 0x10FFFF)) ||
	    (v >= 0xD800 && v <= 0xDFFF)) { *cp = 0xFFFD; return 1; }
	*cp = v;
	return len;
}

static inline int pt_u8enc(uint32_t c, char *o)
{
	if (c < 0x80) { o[0] = (char)c; return 1; }
	if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
	if (c < 0x10000) {
		o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | ((c >> 6) & 0x3F));
		o[2] = (char)(0x80 | (c & 0x3F)); return 3;
	}
	o[0] = (char)(0xF0 | c >> 18); o[1] = (char)(0x80 | ((c >> 12) & 0x3F));
	o[2] = (char)(0x80 | ((c >> 6) & 0x3F)); o[3] = (char)(0x80 | (c & 0x3F)); return 4;
}

/* Letters: ASCII, then the alphabetic blocks below General Punctuation (Latin, Greek,
 * Cyrillic, ... with their combining marks) and the CJK/Hangul range. Emoji, symbols
 * and punctuation are not. */
static inline int pt_is_letter(uint32_t c)
{
	if (c < 0x80) return (c | 32) >= 'a' && (c | 32) <= 'z';
	if (c < 0xC0 || c == 0xD7 || c == 0xF7 || c == 0xFFFD) return 0;
	if (c < 0x2000) return 1;
	if (c >= 0x3040 && c < 0xD800) return 1;
	return 0;
}

static inline int pt_is_apostrophe(uint32_t c) { return c == '\'' || c == 0x2019 || c == 0x2BC; }

/* Lower case for matching; apostrophes become ASCII. */
static inline uint32_t pt_fold(uint32_t c)
{
	if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
	if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
	if (c >= 0x100 && c <= 0x17F) {
		if (c == 0x130) return 'i';
		if (c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F) return c;
		if (c == 0x178) return 0xFF;
		if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c + 1 : c;
		return (c & 1) ? c : c + 1;
	}
	if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
	if (c >= 0x410 && c <= 0x42F) return c + 32;
	if (c >= 0x400 && c <= 0x40F) return c + 80;
	if (c == 0x1E9E) return 0xDF;
	if (c == 0x2019 || c == 0x2BC) return '\'';
	return c;
}

static inline uint32_t pt_upper(uint32_t c)
{
	if (c < 0x80) return (c >= 'a' && c <= 'z') ? c - 32 : c;
	if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
	if (c == 0xFF) return 0x178;
	if (c >= 0x100 && c <= 0x17F) {
		if (c == 0x131) return 'I';
		if (c == 0x130 || c == 0x138 || c == 0x149 || c == 0x17F || c == 0x178) return c;
		if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c : c - 1;
		return (c & 1) ? c - 1 : c;
	}
	if (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2) return c - 32;
	if (c >= 0x430 && c <= 0x44F) return c - 32;
	if (c >= 0x450 && c <= 0x45F) return c - 80;
	return c;
}

static inline int pt_is_upper(uint32_t c) { return pt_fold(c) != c && c != 0x2019 && c != 0x2BC; }

/* Fold a UTF-8 string; returns the output length (out has room for 2*n+1). */
static inline size_t pt_fold_str(const char *s, size_t n, char *out)
{
	size_t i = 0, o = 0;
	while (i < n) {
		uint32_t c;
		i += pt_u8dec((const unsigned char *)s + i, n - i, &c);
		o += pt_u8enc(pt_fold(c), out + o);
	}
	out[o] = 0;
	return o;
}

/* Tokens. A word: letters, with apostrophes between letters. Sentence ends (. ! ? and
 * line breaks) and other breaks (digits, symbols, ; :) cut the context; spaces,
 * commas, quotes, brackets and dashes don't. */
enum { PT_END, PT_WORD, PT_SENT, PT_BREAK };

static inline int pt_transparent(uint32_t c)
{
	switch (c) {
	case ' ': case '\t': case ',': case '"': case '(': case ')': case '[': case ']':
	case '-': case '\'': case 0xA0: case 0xAB: case 0xBB: case 0x2013: case 0x2014:
	case 0x2018: case 0x2019: case 0x201C: case 0x201D: case 0x201E: case 0x2026: case 0x2BC:
		return 1;
	}
	return 0;
}

/* Next token from s[*pos..n): returns its kind; a word is s[*ws .. *ws + *wl). A run of
 * letters longer than PT_WORD_MAX is returned as a break. trailing_apos: a word that
 * ends the text with an apostrophe keeps it (a word still being typed, "don'"). */
static inline int pt_next(const char *s, size_t n, size_t *pos, size_t *ws, size_t *wl, int trailing_apos)
{
	const unsigned char *u = (const unsigned char *)s;
	size_t i = *pos;
	while (i < n) {
		uint32_t c;
		int l = pt_u8dec(u + i, n - i, &c);
		if (pt_is_letter(c)) {
			size_t st = i;
			i += l;
			while (i < n) {
				uint32_t d;
				int k = pt_u8dec(u + i, n - i, &d);
				if (pt_is_letter(d)) { i += k; continue; }
				if (pt_is_apostrophe(d)) {
					uint32_t e;
					if (i + k < n && pt_is_letter((pt_u8dec(u + i + k, n - i - k, &e), e))) { i += k; continue; }
					if (i + k == n && trailing_apos) { i += k; }
				}
				break;
			}
			*pos = i; *ws = st; *wl = i - st;
			return i - st > PT_WORD_MAX ? PT_BREAK : PT_WORD;
		}
		i += l;
		if (c == '.' || c == '!' || c == '?' || c == '\n' || c == '\r') { *pos = i; return PT_SENT; }
		if (pt_transparent(c)) continue;
		*pos = i;
		return PT_BREAK;
	}
	*pos = n;
	return PT_END;
}

#endif
