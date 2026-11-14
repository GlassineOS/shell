#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds a base model for glassine-predict from the pinned sources in LANG.sources:
#   tools/predict-model/build.sh LANG [OUTDIR]
# Downloads go to .cache/predict (checked against their sha256; a changed upstream file
# stops the build), the corpus is extracted and the model built in an Alpine container
# (Docker), the result is OUTDIR/LANG.gpm (default predict-models/) with
# LANG.gpm.license beside it. The same sources give the same bytes.
set -eu
lang=${1:?usage: build.sh LANG [OUTDIR]}
T=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$T/../.." && pwd)
out=${2:-$R/predict-models}
src=$T/$lang.sources
[ -f "$src" ] || { echo "no $src" >&2; exit 1; }
cache=$R/.cache/predict
mkdir -p "$cache" "$out"
out=$(cd "$out" && pwd)

rm -f "$cache/$lang.extract.new"
opts=$(sed -n 's/^options //p' "$src")
corpora=
while read -r kind what file sum url args; do
	[ "$kind" = source ] || continue
	if ! echo "$sum  $cache/$file" | sha256sum -c - >/dev/null 2>&1; then
		echo "fetching $file"
		timeout 1800 curl -fsSL --retry 3 -o "$cache/$file.part" "$url"
		echo "$sum  $cache/$file.part" | sha256sum -c - >/dev/null ||
			{ echo "$file: checksum differs from $lang.sources (upstream changed?)" >&2; rm -f "$cache/$file.part"; exit 1; }
		mv "$cache/$file.part" "$cache/$file"
	fi
	echo "$what /c/$file /c/$lang-$file.txt $args" >> "$cache/$lang.extract.new"
	corpora="$corpora /c/$lang-$file.txt"
done < "$src"
mv "$cache/$lang.extract.new" "$cache/$lang.extract"

img=$(timeout 900 docker build -q -t glassine-predict-model "$T")
# shellcheck disable=SC2086
timeout 3600 docker run --rm --user "$(id -u):$(id -g)" -v "$cache":/c -v "$T":/t:ro -v "$R/src":/src:ro -v "$out":/o "$img" sh -euc '
	while read -r what in txt args; do python3 -I /t/extract.py "$what" "$in" "$txt" $args; done < /c/'"$lang"'.extract
	gcc -O2 -Wall -o /tmp/glassine-predict-build /src/predict/build.c -lm
	/tmp/glassine-predict-build '"$opts"' '"$lang"' /o/'"$lang"'.gpm '"$corpora"
sed -n 's/^licence //p' "$src" > "$out/$lang.gpm.license"
sha256sum "$out/$lang.gpm"
