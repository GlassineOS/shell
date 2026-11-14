# The word predictor (contract version 2)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

`glassine-predict` suggests words for the on-screen keyboard (spec 006, FR-003 to FR-008a): up to
three completions of the word being typed, or after a space the likeliest next words. It is a
separate program behind a line contract, so another predictor can replace it. This repository owns
the contract, the learned words store and the `words` command; the keyboard (GlassineOS/wvkbd) and
the settings app use them. This page is enough to talk to the predictor and to read its files
without its code.

## Running it

The keyboard starts `glassine-predict` with no arguments as a child process, writes requests to
its stdin and reads replies from its stdout, one line each. It runs until stdin closes (or
SIGTERM, SIGINT, SIGHUP), then writes what it learned and exits 0. It never opens a network
socket, and nothing typed appears on stderr.

The keyboard must never wait for it (FR-005): it sends a request, keeps handling keys, and shows a
reply only when its id is the newest it sent. No reply, a late reply or a predictor that is missing
or has exited: no suggestions, typing goes on. A keyboard may restart a predictor that exited.

## Lines

Requests and replies are UTF-8 lines ending in `\n`, fields separated by one tab. In text fields
(context, words) a backslash, tab, newline and carriage return are written `\\`, `\t`, `\n`, `\r`;
any other backslash sequence is an error (`bad-escape`). A request line is at most 32768 bytes; a
longer one is answered with one `error - too-long` and skipped up to its newline, and the lines
after it are read normally.

`ID` is the client's name for the request: 1 to 32 characters of `A-Z a-z 0-9 . _ -`. Every
request gets exactly one reply line carrying its id, in the order of the requests. `LANGS` is the
list of active languages, comma-separated, in order of preference (`en`, `en,de`; each two or
three lowercase letters, then optionally `-` or `_` and more; at most 4, no repeats), which picks
the base models (FR-006). `FIELD` is the field's text-input-v3 content type: the purpose, then its
hints, comma-separated, with the protocol's names (`normal`, `password`,
`normal,auto_capitalization,sensitive_data`).

## Versions

A client starts with `hello ID 2`; the reply names the version the predictor speaks from then on
(the client's, or the newest the predictor has). Without a `hello`, or after `hello ID 1`, the
predictor answers in version 1. Version 2 adds several languages at once: the requests are the
same (a version 1 `LANG` is a list of one), and only the replies differ (marked "v2" below: each
suggested word carries its language, `learn` names the language it learned in). Version 1 is
still answered, so a version 1 client keeps working; its `remove` now removes in every language.

## Requests

| Request | Reply |
|---|---|
| `hello ID VERSION` | `hello ID V`: the contract version from now on (1 or 2) |
| `predict ID LANGS FIELD BEFORE WORD` | v2: `predict ID [W1 L1 [W2 L2 [W3 L3]]]`, each word followed by its language; v1: `predict ID [W1 [W2 [W3]]]`; best first; or `drop ID` |
| `learn ID LANGS FIELD BEFORE WORD PICKED` | v2: `learn ID ok LANG` or `learn ID skip`; v1: `learn ID ok` or `learn ID skip` |
| `learn ID LANGS FIELD WORD` | the same, with no context and nothing picked (version 1's form) |
| `remove ID LANGS WORD` | `remove ID ok` |
| `restore ID LANGS WORD` | `restore ID ok`, or `restore ID none` when WORD wasn't removed |
| anything wrong | `error ID REASON` (`ID` is `-` when the line has no usable id) |

**Active languages:** `languages` in `predict.conf` (below), when it is set and valid, wins over
the request's `LANGS`; the keyboard sends its layout's language, which counts only while the
setting is unset. An empty `LANGS` is allowed only while the setting is set. A language without a
model contributes only learned words.

**predict:** `BEFORE` is the text before the word being typed (what the app reports as
surrounding text before the cursor, up to the protocol's 4 KB, plus what was typed since; FR-003)
and `WORD` the part of the word typed so far. The predictor reads `BEFORE` followed by `WORD`:
the last word of that text, when nothing follows it, is the word being completed, so a client may
also send everything in `BEFORE` and an empty `WORD`. After a space (or other separator) the
words are next words. Only the last 512 bytes of the context count. The words come with the case
to show: a capital typed gives a capital, all capitals typed (two letters or more) give all
capitals, a word normally written with capitals keeps them; with the `auto_capitalization` hint
and nothing typed at a sentence start, the first letter is a capital. Fewer than three words, or
none, is a normal answer.

With several active languages, each language ranks its own candidates by probability, and the
lists merge into one top three: each language's probabilities are weighted by how well that
language explains the context, the words since the sentence start (at most 6 before the word being
typed; bigram probability, a fixed small one for a word the model doesn't know). The weights are
these likelihoods normalized, none below 2 %, the earlier language ahead on a tie, so with no
context (a sentence start) both languages are offered and a few words of one language make it
lead. A word both languages have counts once, with the sum of its shares and the language with
the larger share.

When several `predict` requests are read together (the keyboard was faster than the predictor),
only the newest is answered with words; the older ones get `drop ID`. Other requests between them
are carried out in order.

**learn:** the keyboard sends it for each word the person finished (a space or punctuation after
it) or picked from the suggestions, with the text before the word as `BEFORE`, and as `PICKED` the
language of the suggestion it picked (empty when the word was typed). `skip`: the field doesn't
learn (below), learning is off, the word was removed, or it isn't a word (one letter, digits,
symbols, more than 48 bytes). A word the base model doesn't know is suggested once it was learned
twice (Scenario 6); among the completions of a word being typed, the person's words move up with
use (next words after a space come from the base model and the context alone). A learned word is
shown as last typed, except that a capital only at its first letter (a sentence start) doesn't
replace a lowercase spelling; words of the base model keep the model's spelling. Learned words
count at once; they are written to the store 2 s after the last request, at most 30 s after the
first one waiting, after 64 waiting words, and at exit.

**Which language learns** a word (the reply names it), the first rule that applies:

1. `PICKED`, when it is one of the active languages: a picked suggestion learns in its language.
2. The only active language that knows the word (its model or its learned words).
3. Of the languages that know it (or of all, when none does), the one whose model best explains
   the words before it in `BEFORE` (as for the merge above).
4. With no context: the first that knows it, else the first active language.

**remove** (FR-008a, the keyboard's hold on a suggestion): `WORD` is never suggested again in any
language, whether it was learned or comes from a base model, and is not learned again; `LANGS` is
checked but doesn't narrow it. Learned counts of it in every language are dropped. Typing the word
stays possible. Written at once; the next `predict` already leaves it out. **restore** puts a
removed word back, in every language.

Error reasons: `bad-line` (no verb, wrong number of fields, a NUL byte, a `hello` without a
number), `bad-id`, `unknown-verb`, `bad-escape`, `bad-lang` (a bad list, or no list and no
setting), `bad-field`, `bad-word`, `too-long`, `store` (the learned words file can't be read or
written; nothing was changed), `busy` (more than 4096 lines read at once).

## Private fields (FR-007)

The predictor applies the rule itself, whatever the keyboard sends:

| Field | Words | Learns |
|---|---|---|
| purpose `password` or `pin`, or the hint `sensitive_data` or `hidden_text` | none | no |
| purpose `normal`, `alpha`, `name` | yes | yes |
| purpose `terminal` | yes | no |
| purpose `email` or `url` | by the `addresses` setting (below): `off` none, `suggest` (the default) yes, `learn` yes | only with `learn` |
| `digits`, `number`, `phone`, `date`, `time`, `datetime` | none | no |
| a purpose or hint this version doesn't know | yes | no |

In a private field the reply is an empty `predict ID` or `learn ID skip`, and nothing is read
from or written to the store, not even a directory. A keyboard should not send private text at
all; this is the second line of defence. A FIELD with characters other than lowercase letters and
`_` in its names is `bad-field`.

## The learned words store (version 1)

`$XDG_DATA_HOME/glassine/predict/words`, or `~/.local/share/glassine/predict/words` when
`XDG_DATA_HOME` is unset or not absolute. The directory is mode 0700, the file 0600 (whatever the
umask); `lock` beside it is the lock (`flock`, exclusive) every writer holds for its
read-modify-write; `words.tmp` is the file being written before it replaces `words`. The files are
created by the first word learned or removed, never by a private field. The store never leaves the
device.

```
glassine-words 1 3
w	en	Glassine	4	20406
w	en	tea	12	20410
x	*	tomorrow
```

The first line: the format and its version, then the erase count (bumped by each erase, so a
running predictor drops words it learned before the erase and hadn't written yet). Then one line
per word, tab-separated: `w LANG WORD COUNT DAY` a learned word as last typed, its number of uses
and the day of its last use (days since 1970-01-01, UTC); `x LANG WORD` a removed word: it counts in every
language, whatever `LANG` says (written as `*`; version 1 of the predictor could write one
language there). The learned words come first, then the removed ones, each sorted by
language and then by the word folded to lowercase; words match folded. A reader skips lines it doesn't understand; a writer refuses a file whose first line
it doesn't understand, and leaves it as it is. At most 20000 learned words are kept; beyond that the
least used go first, a use counting half after 90 days.

The settings are in `$XDG_CONFIG_HOME/glassine/predict.conf` (or
`~/.config/glassine/predict.conf`), `KEY = VALUE` lines; the last line of a key wins, other lines
are ignored. The predictor reads them at each request that needs them, so a change counts from the
next request.

| Key | Values | Default |
|---|---|---|
| `learning` | `off` (also `false`, `0`) stops learning everywhere; anything else is on | on |
| `languages` | the active languages in order, `en,de` (spaces allowed); wins over the keyboard's list. An invalid list counts as unset | unset: the keyboard's layout language |
| `addresses` | email and URL fields: `off` no words and no learning; `suggest` words from the base model and the words already learned, nothing typed there is learned; `learn` words and learning as in a normal field. Anything else is `suggest` | `suggest` |

Private hints win over `addresses` (an email field marked `sensitive_data` stays private), and a
hint this version doesn't know still stops learning. Turning learning off keeps what was learned;
erasing is separate.

## The words command

For the settings app and anything else that shows or changes the learned words. It works on the
same store under the same lock, so a running predictor follows it at its next request.

```
glassine-predict words list [-l LANG] [-s TEXT]   learned words, most used first
glassine-predict words removed [-s TEXT]          removed words
glassine-predict words remove WORD                remove, in every language
glassine-predict words restore WORD               put back, in every language
glassine-predict words erase                      forget everything learned and the removed list
```

`list` prints `LANG WORD COUNT DAY`, tab-separated, one per line, each word with the language it
was learned in (`-l` keeps one language); `removed` prints one `WORD` per line; `-s` keeps the words that contain TEXT (folded). `erase` empties the learned words and the removed
list; the handwriting samples belong to the handwriting recognizer, whose own erase the settings
call too (FR-008). Exit status 0, 1 when the store can't be read or written (a message on stderr),
2 for a usage error.

## Base models

One file per language: `/usr/share/glassine/predict/LANG.gpm` (`$GLASSINE_PREDICT_MODELS`
replaces the directory), read-only through mmap, with its licence and attributions in
`LANG.gpm.license` beside it. No model for a language: learned words only. A file that isn't a
valid model is ignored with one line on stderr. The format is the header of
`src/predict/gpm.h`: the vocabulary sorted by folded word (a prefix is one binary search), each
word's likeliest next words, and the likeliest next words after word pairs, with probabilities in
one byte each. The ranking interpolates the three levels and adds the person's use of each word.

### Building one

`make predict-model` (or `tools/predict-model/build.sh LANG [OUTDIR]`) builds `predict-models/LANG.gpm`
from `tools/predict-model/LANG.sources`: the downloads with their sha256 (a changed upstream file
stops the build), the builder's options and the licence text. Docker runs the corpus extraction
(`extract.py`) and the builder (`src/predict/build.c`) in Alpine; the same sources give the same
bytes. Every 200th line of each corpus is held out, and `glassine-predict bench LANGS FILE` measures
on it: the time of each prediction while typing every word letter by letter, and how often the
next word is among the three; with `-l` it learns every typed word on the way, as the keyboard
would (in memory; the person's store is never used).

English (`en.sources`): the Simple English Wikipedia (the `wikimedia/wikipedia` dataset at a fixed
revision; CC BY-SA 3.0 and GFDL) and the English sentences of Tatoeba (an archived export at a fixed
snapshot; CC BY 2.0 FR) without the sentences that use the corpus' two stock names, which would
otherwise lead every next-word list. The model is licensed CC BY-SA 3.0, not under this
repository's GPL. 40000 words, about 5 MB.

German (`de.sources`): one of the 20 parts of the German Wikipedia in the same dataset and
revision (CC BY-SA 3.0 and GFDL) and the German sentences of Tatoeba from an archived snapshot of
its all-languages export (CC BY 2.0 FR), without the sentences that use its stock names (Tom,
Maria and their genitives: one sentence in six). No single-letter words. Nouns keep their capital
because a word shows its most used spelling inside sentences. CC BY-SA 3.0, 40000 words, about
6 MB.

Another language needs only a `LANG.sources` of the same kind: a Wikipedia edition from the same
dataset, Tatoeba's sentences in that language, and a stock-name filter if its sentences have one.
Never use a corpus whose licence doesn't allow redistribution.
