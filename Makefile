# glassine-shell-wl: the bar, launcher, overview, quick settings
# glassine-swayd: the window model over sway's IPC
S = src
OUT ?= .

all: $(OUT)/glassine-shell-wl $(OUT)/glassine-swayd

$(OUT)/glassine-shell-wl: $(wildcard $(S)/glassine-shell*) $(S)/wayland/sway-ipc.c $(S)/wayland/build-shell-wl.sh
	sh $(S)/wayland/build-shell-wl.sh $(S) $@

$(OUT)/glassine-swayd: $(S)/glassine-swayd.c $(S)/glassine-popupd.h $(S)/wayland/swayd-handles.c $(S)/wayland/sway-ipc.c $(S)/wayland/build-swayd.sh
	sh $(S)/wayland/build-swayd.sh $(S) $@

# The tests build what they need in their own containers.
check:
	sh test/swayd/test.sh
	sh test/settings/test.sh
	sh test/setup/test.sh
	sh test/admin-auth/test.sh
	sh test/health/test.sh

clean:
	rm -f $(OUT)/glassine-shell-wl $(OUT)/glassine-swayd

.PHONY: all check clean
