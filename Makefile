# glassine-shell-wl: the bar, overview, divider mode, quick settings
# glassine-swayd: the window model over sway's IPC
# glassine-bgprio: hidden apps deprioritized, frozen and paged out
# glassine-zpark: one parked app's zram slots to disk (glassine-zram park)
S = src
OUT ?= .
CFLAGS ?= -O2 -Wall

all: $(OUT)/glassine-shell-wl $(OUT)/glassine-swayd $(OUT)/glassine-bgprio $(OUT)/glassine-zpark

$(OUT)/glassine-shell-wl: $(wildcard $(S)/glassine-shell*) $(S)/wayland/sway-ipc.c $(S)/wayland/build-shell-wl.sh
	sh $(S)/wayland/build-shell-wl.sh $(S) $@

$(OUT)/glassine-swayd: $(S)/glassine-swayd.c $(S)/glassine-popupd.h $(S)/wayland/swayd-handles.c $(S)/wayland/sway-ipc.c $(S)/wayland/build-swayd.sh
	sh $(S)/wayland/build-swayd.sh $(S) $@

# needs json-c-dev
$(OUT)/glassine-bgprio: $(S)/glassine-bgprio.c $(S)/wayland/sway-ipc.c $(S)/wayland/sway-ipc.h
	$(CC) $(CFLAGS) -o $@ $(S)/glassine-bgprio.c $(S)/wayland/sway-ipc.c `pkg-config --cflags --libs json-c` -lpthread

$(OUT)/glassine-zpark: $(S)/glassine-zpark.c
	$(CC) $(CFLAGS) -o $@ $(S)/glassine-zpark.c

# The tests build what they need in their own containers. First: the GTK themes and the
# keyboard looks in rootfs/ are GlassineOS/ui's (tools/gen-apps; test/ui.sh finds ui).
check:
	python3 "$$(sh test/ui.sh)/tools/gen-apps" --check rootfs
	sh test/swayd/test.sh
	sh test/shell/test.sh
	sh test/settings/test.sh
	sh test/setup/test.sh
	sh test/admin-auth/test.sh
	sh test/health/test.sh
	sh test/session-dispatch/run.sh
	sh test/bgprio/test.sh
	sh test/zram/test.sh

clean:
	rm -f $(OUT)/glassine-shell-wl $(OUT)/glassine-swayd $(OUT)/glassine-bgprio $(OUT)/glassine-zpark

.PHONY: all check clean
