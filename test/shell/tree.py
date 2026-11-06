# sway's tree for the shell test (test.sh), read from `swaymsg -t get_tree -r` on stdin.
#   tree.py id APP_ID         the window's container id (hex, as the shell's verbs have it)
#   tree.py count             tiled windows per screen, in screen order: "1 2 3 4"
#   tree.py narrow|wide NUM   a window on screen NUM narrower than the screen /
#                             as wide as it but not as tall (a full-width half); exit 1: none
#   tree.py div NUM           screen NUM's dividers as glassine-swayd sends them (one word a line:
#                             "N,ORIENT0" then "x,y,w,h,vert,level,a,b,pos" in device px, scale 2)
import json
import sys

t = json.load(sys.stdin)
cmd = sys.argv[1]
SC = 2


def views(n):
    if n.get("shell"):
        return [n]
    return [v for c in n.get("nodes", []) for v in views(c)]


def workspaces(n, out):
    if n.get("type") == "workspace" and n.get("name") != "__i3_scratch":
        out.append(n)
    for c in n.get("nodes", []):
        workspaces(c, out)
    return out


wss = sorted(workspaces(t, []), key=lambda w: w.get("num", 1 << 30))


def ws(num):
    for w in wss:
        if str(w.get("num")) == str(num):
            return w
    sys.exit(1)


if cmd == "id":
    def find(n):
        if n.get("app_id") == sys.argv[2]:
            return n
        for c in n.get("nodes", []) + n.get("floating_nodes", []):
            r = find(c)
            if r:
                return r
    n = find(t)
    if not n:
        sys.exit(1)
    print("%x" % n["id"])
elif cmd == "count":
    print(" ".join(str(len(views(w))) for w in wss))
elif cmd in ("narrow", "wide"):
    w = ws(sys.argv[2])
    W, H = w["rect"]["width"], w["rect"]["height"]
    for v in views(w):
        r = v["rect"]
        if cmd == "narrow" and r["width"] < .9 * W:
            print("%x" % v["id"]); break
        if cmd == "wide" and r["width"] >= .9 * W and r["height"] < .9 * H:
            print("%x" % v["id"]); break
    else:
        sys.exit(1)
elif cmd == "div":
    w = ws(sys.argv[2])
    root = w
    while True:
        kids = [c for c in root.get("nodes", []) if views(c)]
        if len(kids) == 1 and not kids[0].get("shell"):
            root = kids[0]
            continue
        break
    vs = views(w)
    side = root.get("layout") == "splith"
    out = ["%d,%d" % (len(vs), 1 if side else 0)]

    def bar(r0, r1, along, vert, level, a, b, span):
        if vert:
            x0, x1 = r0["x"] + r0["width"], r1["x"]
            x, wd = (x0, x1 - x0) if x1 - x0 > 0 else (x0 - 10, 20)
            y, h = along["y"], along["height"]
            pos = r0["width"] / max(1, span - wd)
        else:
            y0, y1 = r0["y"] + r0["height"], r1["y"]
            y, h = (y0, y1 - y0) if y1 - y0 > 0 else (y0 - 10, 20)
            x, wd = along["x"], along["width"]
            pos = r0["height"] / max(1, span - h)
        out.append("%d,%d,%d,%d,%d,%d,%x,%x,%.3f" % (x * SC, y * SC, wd * SC, h * SC, vert, level, a, b, pos))

    if len(kids) >= 2:
        k0, k1 = kids[0], kids[1]
        bar(k0["rect"], k1["rect"], root["rect"], 1 if side else 0, 0, views(k0)[0]["id"], views(k1)[0]["id"],
            root["rect"]["width"] if side else root["rect"]["height"])
        for gi, k in enumerate(kids[:2]):
            kv = views(k)
            if len(kv) == 2 and not k.get("shell"):
                inner_side = k.get("layout") == "splith"
                bar(kv[0]["rect"], kv[1]["rect"], k["rect"], 1 if inner_side else 0, gi + 1, kv[0]["id"], kv[1]["id"],
                    k["rect"]["width"] if inner_side else k["rect"]["height"])
    print("\n".join(out))
