#!/bin/sh
# The text fonts of glassine-shell's themes (design "E-ink Shell"): white = IBM Plex
# Sans/Mono, editorial = Source Serif 4, stationery = EB Garamond (that one is
# Alpine's font-eb-garamond). Icons are the installed icon themes (glassine-mono,
# Adwaita), not a font. Emoji: Noto Emoji, the monochrome variable font (Alpine
# only packages colour emoji); fontconfig's 45-generic.conf already maps it to
# the "emoji" family.
#
# fetch-fonts.sh FONTDIR (-> /usr/share/fonts/glassine on the device)
#
# Pinned to google/fonts commits + sha256. Needs curl or wget, sha256sum.
set -eu
FD=$1
mkdir -p "$FD"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
GF=https://raw.githubusercontent.com/google/fonts/0b58fb370093f9a9f4ff785d94405710b79de67c/ofl
get() {   # url file sha256
    if command -v curl >/dev/null; then curl -sfL -o "$T/$2" "$1"; else wget -q -O "$T/$2" "$1"; fi
    echo "$3  $T/$2" | sha256sum -c - >/dev/null || { echo "fetch-fonts: bad checksum: $2" >&2; exit 1; }
}
get "$GF/ibmplexsans/IBMPlexSans%5Bwdth%2Cwght%5D.ttf" IBMPlexSans-VF.ttf 3b031aa4216174205bd8471f88a49b91f093169e9e87bd5262242bc5967fe2e3
get "$GF/ibmplexmono/IBMPlexMono-Regular.ttf" IBMPlexMono-Regular.ttf 6a3412f058c7d8dfd9170c41e85ade48e5156ecb89356110ca57a0a27734af46
get "$GF/ibmplexmono/IBMPlexMono-Medium.ttf" IBMPlexMono-Medium.ttf a9b4c49bb299e05b5f6c481e7fb5e78943d2793249a0c8874ab574a2d1ea6755
get "$GF/ibmplexmono/IBMPlexMono-SemiBold.ttf" IBMPlexMono-SemiBold.ttf d3c38e55c78f5b0f28009fddba4834ec503278936a5986032424c9bd2d23aa46
get "$GF/sourceserif4/SourceSerif4%5Bopsz%2Cwght%5D.ttf" SourceSerif4-VF.ttf 97b2d4da6e3cb494b5a1e66ae176914d852ccabef49e0c02c0df25f3e39aca0b
get "$GF/sourceserif4/SourceSerif4-Italic%5Bopsz%2Cwght%5D.ttf" SourceSerif4-Italic-VF.ttf 15fbc7e4679489a501998c3669272637a6646388ef7e4bd77eebb5bf967a1f42
get "$GF/notoemoji/NotoEmoji%5Bwght%5D.ttf" NotoEmoji-VF.ttf de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551
cp "$T"/IBMPlex*.ttf "$T"/SourceSerif4*.ttf "$T"/NotoEmoji*.ttf "$FD/"

echo "fetch-fonts: $(ls "$FD" | wc -l) fonts in $FD"
