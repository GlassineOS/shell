# A target of the shell's hit dump (GLASSINE_SHELL_HITS): "NAME ARG WIN X Y W H".
#   hit.py FILE NAME [ARG|-] [WIN|-] [N]   the N-th match's centre "X Y" (logical px)
#   hit.py --count FILE NAME [ARG|-] [WIN|-]
#   hit.py --all FILE NAME                 every match: "ARG WIN CX CY"
import sys

a = sys.argv[1:]
mode = a.pop(0) if a and a[0].startswith("--") else ""
f, name = a[0], a[1]
arg = a[2] if len(a) > 2 else "-"
win = a[3] if len(a) > 3 else "-"
nth = int(a[4]) if len(a) > 4 else 0
m = []
try:
    lines = open(f).read().split("\n")
except OSError:
    lines = []
for ln in lines:
    p = ln.split()
    if len(p) != 7 or p[0] != name:
        continue
    if arg != "-" and p[1] != arg:
        continue
    if win != "-" and p[2] != win:
        continue
    x, y, w, h = map(float, p[3:])
    m.append((p[1], p[2], x + w / 2, y + h / 2))
if mode == "--count":
    print(len(m))
elif mode == "--all":
    for r in m:
        print("%s %s %.0f %.0f" % r)
elif len(m) > nth:
    print("%.0f %.0f" % (m[nth][2], m[nth][3]))
else:
    sys.exit(1)
