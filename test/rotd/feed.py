# the stand-in sensor: 10 samples a second into the FIFO (x/y/z s32, scale 0.001) of
# the pose in /tmp/pose ("X Y Z" in m/s2, or "none": paused, nothing written); when
# rotd closes the device (lock mode) it waits for the next open
import os, struct, sys, time
fd = os.open(sys.argv[1], os.O_WRONLY)
while True:
    try:
        p = open("/tmp/pose").read().split()
    except OSError:
        p = ["none"]
    if p and p[0] != "none":
        try:
            os.write(fd, struct.pack("<3i", *(int(float(v) * 1000) for v in p[:3])))
        except BrokenPipeError:
            os.close(fd)
            fd = os.open(sys.argv[1], os.O_WRONLY)
    time.sleep(0.1)
