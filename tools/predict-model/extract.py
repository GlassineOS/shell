# SPDX-License-Identifier: GPL-2.0-or-later
# The model corpus as plain text, one paragraph or sentence per line (see build.sh).
# Every 200th line goes to OUT.test instead: held out of the model, for measuring it.
#   extract.py wikipedia PARQUET OUT   the "text" column, paragraphs of 4 words or more
#   extract.py tatoeba-all TAR.BZ2 OUT LANG [NAME...]
#                                       the same from the all-languages export
#                                       (sentences.csv), the sentences of LANG only
#   extract.py tatoeba TSV.BZ2 OUT [NAME...]
#                                       the sentence column of a Tatoeba per-language export,
#                                       without sentences that use one of the NAMEs (the
#                                       English sentences use a few stock names so often
#                                       that they would be the likeliest next words)
import bz2
import sys

kind, src, out = sys.argv[1:4]
drop = set(sys.argv[4:])
n = 0


class Split:
    def __init__(self, path):
        self.train = open(path, "w", encoding="utf-8")
        self.test = open(path + ".test", "w", encoding="utf-8")
        self.n = 0

    def write(self, line):
        self.n += 1
        (self.test if self.n % 200 == 0 else self.train).write(line)

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.train.close()
        self.test.close()


with Split(out) as o:
    if kind == "wikipedia":
        import pyarrow.parquet as pq
        f = pq.ParquetFile(src)
        for b in f.iter_batches(columns=["text"], batch_size=2000):
            for text in b.column(0).to_pylist():
                for line in text.split("\n"):
                    line = line.strip()
                    # headings, list stubs and table rows are not running text
                    if len(line.split()) < 4 or line.startswith(("|", "{", "!", "*")):
                        continue
                    o.write(line + "\n")
                    n += 1
    elif kind in ("tatoeba", "tatoeba-all"):
        if kind == "tatoeba":
            rows = bz2.open(src, "rt", encoding="utf-8")
            lang = None
        else:
            import tarfile
            tar = tarfile.open(src, "r:bz2")
            member = tar.extractfile("sentences.csv")
            import io
            rows = io.TextIOWrapper(member, encoding="utf-8")
            lang, drop = sys.argv[4], set(sys.argv[5:])
        for row in rows:
            parts = row.rstrip("\n").split("\t")
            if len(parts) < 3 or not parts[2].strip() or (lang and parts[1] != lang):
                continue
            words = set(w.strip(".,!?;:'\"()\u201e\u201c\u00bb\u00ab") for w in parts[2].split())
            words |= set(w.split("'")[0] for w in words)
            if drop & words:
                continue
            o.write(parts[2].strip().replace("\r", " ") + "\n")
            n += 1
    else:
        sys.exit("unknown corpus kind " + kind)
print(f"{out}: {n} lines", file=sys.stderr)
