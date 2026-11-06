# The stand-in for glassine-swayd's socket (test.sh): every datagram the shell
# sends is logged as a line (NULs as spaces) and passed on to the real one.
#   proxy.py LISTEN FORWARD LOG
import os
import socket
import sys

src, dst, log = sys.argv[1:4]
try:
    os.unlink(src)
except FileNotFoundError:
    pass
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
s.bind(src)
o = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
while True:
    m = s.recv(4096)
    with open(log, "a") as f:
        f.write(m.decode(errors="replace").replace("\0", " ") + "\n")
    try:
        o.sendto(m, dst)
    except OSError:
        pass
