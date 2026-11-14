#!/bin/sh
# (see test.sh) inside the container: /src = src, /t = this dir
set -eu
gcc -O2 -Wall -Wextra -Werror -o /tmp/glassine-predict /src/glassine-predict.c /src/predict/store.c -lm
gcc -O2 -Wall -Wextra -Werror -o /tmp/glassine-predict-build /src/predict/build.c -lm
mkdir -p /tmp/models
/tmp/glassine-predict-build -s ai -m 1 -B 1 -T 1 -P 1 en /tmp/models/en.gpm /t/corpus.txt 2>/dev/null
/tmp/glassine-predict-build -m 1 -B 1 -T 1 -P 1 de /tmp/models/de.gpm /t/corpus-de.txt 2>/dev/null
python3 -I /t/test.py /tmp/glassine-predict /tmp/models
