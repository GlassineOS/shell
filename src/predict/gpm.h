// SPDX-License-Identifier: GPL-2.0-or-later
// The base model file of glassine-predict (one per language, format version 1;
// docs/predictor.md). Little-endian, read through mmap; every offset is in bytes from
// the start of the file and 4-byte aligned. Probabilities are quantized to one byte:
// q = round(-8 * log2(p)), clipped to 255, so p = 2^(-q/8).
#ifndef GLASSINE_PREDICT_GPM_H
#define GLASSINE_PREDICT_GPM_H

#include <stdint.h>

#define GPM_MAGIC "GLPM"
#define GPM_VERSION 1

struct gpm_header {
	char magic[4];
	uint32_t version;
	char lang[8];           /* NUL-padded language tag */
	uint32_t nvocab;        /* words: ids 0..nvocab-1, sorted by folded key (bytes);
	                           id nvocab is the sentence start */
	uint32_t off_vocab;     /* nvocab struct gpm_word */
	uint32_t off_str, len_str;      /* NUL-terminated UTF-8 strings */
	uint32_t off_bi;        /* nvocab+2 uint32: context id i has successors
	                           bisucc[bi[i] .. bi[i+1]) */
	uint32_t off_bisucc, nbisucc;   /* uint32 id << 8 | q, likeliest first */
	uint32_t off_tri, ntri;         /* struct gpm_tri sorted by (a, b) */
	uint32_t off_trisucc, ntrisucc; /* as bisucc; entry t ends where t+1 starts */
	uint32_t off_top, ntop;         /* likeliest words without context, as bisucc */
	uint32_t reserved[4];
};

struct gpm_word {
	uint32_t key;           /* folded form (string offset in the string block) */
	uint32_t disp;          /* the form to show (most frequent spelling) */
	uint8_t q;              /* unigram probability */
	uint8_t pad[3];
};

struct gpm_tri {
	uint32_t a, b;          /* the two words before, older first */
	uint32_t start;         /* first successor in trisucc */
};

#define GPM_ID(e) ((e) >> 8)
#define GPM_Q(e) ((e) & 0xFF)

#endif
