# glassine-shell: the bar, overview, divider mode, quick settings
# glassine-swayd: the window model over sway's IPC
# glassine-bgprio: hidden apps deprioritized, frozen and paged out
# glassine-zpark: one parked app's zram slots to disk (glassine-zram park)
# glassine-sleep-pill: the sleep overlay the panel daemon draws over the last page
# libglassine-ink.so: the GTK 3 module that announces GTK apps' pen canvases
# glassine-rotd: screen autorotate from the accelerometer
# glassine-predict: the on-screen keyboard's word predictor and the learned words command
S = src
OUT ?= .
CFLAGS ?= -O2 -Wall

all: $(OUT)/glassine-shell $(OUT)/glassine-swayd $(OUT)/glassine-bgprio $(OUT)/glassine-zpark \
	$(OUT)/glassine-sleep-pill $(OUT)/libglassine-ink.so $(OUT)/glassine-rotd $(OUT)/glassine-predict

$(OUT)/glassine-shell: $(wildcard $(S)/glassine-shell*) $(S)/wayland/sway-ipc.c $(S)/wayland/build-shell.sh
	sh $(S)/wayland/build-shell.sh $(S) $@

$(OUT)/glassine-swayd: $(S)/glassine-swayd.c $(S)/glassine-popupd.h $(S)/wayland/swayd-handles.c $(S)/wayland/sway-ipc.c $(S)/wayland/build-swayd.sh
	sh $(S)/wayland/build-swayd.sh $(S) $@

# needs json-c-dev
$(OUT)/glassine-bgprio: $(S)/glassine-bgprio.c $(S)/wayland/sway-ipc.c $(S)/wayland/sway-ipc.h
	$(CC) $(CFLAGS) -o $@ $(S)/glassine-bgprio.c $(S)/wayland/sway-ipc.c `pkg-config --cflags --libs json-c` -lpthread

$(OUT)/glassine-zpark: $(S)/glassine-zpark.c
	$(CC) $(CFLAGS) -o $@ $(S)/glassine-zpark.c

$(OUT)/glassine-rotd: $(S)/glassine-rotd.c
	$(CC) $(CFLAGS) -o $@ $(S)/glassine-rotd.c -lm

$(OUT)/glassine-predict: $(S)/glassine-predict.c $(S)/predict/store.c $(S)/predict/store.h $(S)/predict/text.h $(S)/predict/gpm.h
	$(CC) $(CFLAGS) -o $@ $(S)/glassine-predict.c $(S)/predict/store.c -lm

# The base models (docs/predictor.md): downloads pinned sources (about 180 MB), builds in
# Docker, writes predict-models/LANG.gpm and its licence. Not part of all or check.
PREDICT_LANGS ?= en
predict-model:
	for l in $(PREDICT_LANGS); do sh tools/predict-model/build.sh $$l || exit 1; done

# needs GlassineOS/ui (pkg-config glassine-ui) and pango-dev
$(OUT)/glassine-sleep-pill: $(S)/glassine-sleep-pill.c
	$(CC) $(CFLAGS) -o $@ $(S)/glassine-sleep-pill.c `pkg-config --cflags --libs glassine-ui pangocairo` -lm

# needs gtk+3.0-dev and wayland-dev; the protocol is the copy in wayland/protocols
$(OUT)/libglassine-ink.so: $(S)/wayland/glassine-ink-gtk.c $(S)/wayland/protocols/glassine-ink-v1.xml $(S)/wayland/build-ink.sh
	sh $(S)/wayland/build-ink.sh $(S) $@

# The tests build what they need in their own containers. First: the GTK themes and the
# keyboard looks in rootfs/ are GlassineOS/ui's (tools/gen-apps; test/ui.sh finds ui).
check:
	python3 "$$(sh test/ui.sh)/tools/gen-apps" --check rootfs
	sh test/sleep-pill/test.sh
	sh test/ink/test.sh
	sh test/swayd/test.sh
	sh test/shell/test.sh
	sh test/e2e/test.sh
	sh test/settings/test.sh
	sh test/setup/test.sh
	sh test/admin-auth/test.sh
	sh test/migrate/test.sh
	sh test/health/test.sh
	sh test/session-dispatch/run.sh
	sh test/bgprio/test.sh
	sh test/zram/test.sh
	sh test/rotd/test.sh
	sh test/predict/test.sh

clean:
	rm -f $(OUT)/glassine-shell $(OUT)/glassine-swayd $(OUT)/glassine-bgprio $(OUT)/glassine-zpark \
		$(OUT)/glassine-sleep-pill $(OUT)/libglassine-ink.so $(OUT)/glassine-rotd $(OUT)/glassine-predict

.PHONY: all check clean predict-model
