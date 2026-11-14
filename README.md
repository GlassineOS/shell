# GlassineOS/shell

The desktop around the apps on an e-ink device running sway:

- **`glassine-shell`:** the bar (Home button, screen button), Overview, divider mode, quick settings, power view and notifications, drawn natively. Its requests and the verbs it sends to `glassine-swayd` are listed at the top of `src/glassine-shell.c`.
- **`glassine-swayd`:** the window model over sway's IPC: tiles, drag handles, dividers and when the on-screen keyboard shows.
- **`glassine-settings`:** the touch settings app (Python, GTK3, cairo), with its root helper.
- **`glassine-setup`:** the first-run setup, with its root helper.
- **The session:** `glassine-session`, `glassine-session-sway`, the sway config and key bindings, the on-screen keyboard wrappers and the `glassine-mono` icons.
- **Background apps:** `glassine-bgprio` (started with the sway session) gives apps on hidden workspaces CPU time only when the apps in front don't want it, freezes them after a while (not media players, apps playing audio, Flatpaks with the background permission or the desktop itself) and pages their memory out; `glassine-zpark` moves one parked app's swapped memory to disk.
- **Autorotate:** the OpenRC service `glassine-rotd` turns the sway output with the device (an IIO accelerometer), or to the fixed turn picked in the quick settings or the settings app (`/etc/glassine/rotation`); the e-ink daemon holds the old picture until the turned desktop has redrawn. The device's values are in `/etc/glassine/rotd.conf` (below). Spec: `specs/005-autorotate`.
- **Compressed swap:** the OpenRC service `glassine-zram` (`/usr/sbin/glassine-zram start|stop|park|writeback|status`): zram swap whose parked apps spill to a compressed btrfs image on the user data partition, under `<USERDATA_PREFIX>/zram/` from `/etc/glassine/deploy.conf` (RAM only when userdata isn't mounted or short of space). Host-tested only: the freeze, page-out and spill chain hasn't run on a device from this repository.
- **Pen canvases:** `glassine-ink`, a GTK 3 module (`libglassine-ink.so`, loaded through the session's `GTK_MODULES=glassine-ink`): GTK 3 apps with a known canvas widget (`GtkXournal`; `GLASSINE_INK_WIDGETS=Type,Type` replaces the list) announce the visible canvas as the window's ink area over the `glassine_ink_v1` protocol (the copy in `src/wayland/protocols`, from GlassineOS/protocols); their other windows get none. Off without that protocol. GTK 4 has no modules: `glassine-ink-discover` covers it.
- **The sleep pill:** `glassine-sleep-pill` draws the pill the panel daemon shows over the last page while the device sleeps (the device's power daemon runs it before suspend), in the shell's voice and turned with the screen; the screen size comes from the device's panel adapter (`/etc/glassine/epd.conf`).
- **Word suggestions:** `glassine-predict`, the on-screen keyboard's default predictor (spec 006, FR-005 to FR-008a): completions and next words from the text before the cursor, from per-language base models (several languages at once, merged by which one the text reads like) and the words the person typed (never from private fields), behind a line contract the keyboard never waits on; `glassine-predict words` lists, removes, puts back and erases the learned words for the settings. Contract, files and model recipe: `docs/predictor.md`. Host-tested only: the keyboard and the settings don't use it yet.
- **Session services:** `glassine-pipewire` (PipeWire with the saved volume), `glassine-bt-trust` (trusts devices paired outside the settings), `glassine-ink-discover` (finds pen canvases over AT-SPI), `glassine-appenv` (per-app render profiles).
- **Themes:** the `glassine-white`, `glassine-editorial` and `glassine-stationery` GTK 3 themes and their libadwaita CSS (`usr/share/glassine/gtk4`, applied by `glassine-theme-apply`). They are generated from GlassineOS/ui's voice data (`tools/gen-apps`), as are the GTK text faces in `glassine-theme-apply` and the keyboard looks in `glassine-wvkbd` (the lines between their `gen-apps` markers): change them there, then run `tools/gen-apps rootfs` from a ui checkout.
- **System files the session needs:** the doas and polkit rules for its helpers and Wi-Fi, `glassine-checkpass` (admin password check), the package-index refresh on connect and a boot health check.

**This repository isn't used for builds yet.** Images still come from the private lab tree, and this tree is exported from it. Until the cutover, a change made here has to go back to the lab by hand, or the next export overwrites it.

## Build and test

```
make            # glassine-shell, glassine-swayd, glassine-bgprio, glassine-zpark, glassine-rotd, glassine-sleep-pill,
                # glassine-predict and libglassine-ink.so, on Alpine
make check      # compiles, then the host tests in Docker
make predict-model   # the base word models (pinned downloads, Docker): predict-models/LANG.gpm
```

`make check` first checks that the generated themes are current with GlassineOS/ui (`tools/gen-apps --check`; `test/ui.sh` finds ui: `$GLASSINE_UI`, a `../ui` checkout beside this repository, else the version pinned in `test/ui.ref`, which CI uses; a PR bumps that file to take a new ui), then runs `test/sleep-pill` (the three voices and four turns, each output checked as `glassine-epdd-desktop` reads it), `test/ink` (the GTK module under stock sway, where it must stay off, and under the package repository's `sway-glassine`, whose ink report must be exactly the visible canvas; that part needs an arm64 host or qemu-user, else it is skipped), `test/settings` (renders every page), `test/setup` (the first-run flow and its root helper), `test/swayd` (the window model on stock sway, headless), `test/shell` (the bar, Overview and divider mode on stock sway with screenshots of each voice, portrait and landscape, in `test/shell/out/shots`), `test/e2e` (the shell and `glassine-swayd` together on stock sway: divider mode from swayd's own list, swaps, Home, the empty screen and launches with their argument), `test/admin-auth` (the administrator tiers of the root helpers, with real accounts), `test/health` (the boot health check with BusyBox), `test/session-dispatch` (the session's restart loop), `test/bgprio` (glassine-bgprio on headless sway in a privileged container with its own cgroup v2 tree; the page-out check is skipped on a host without swap) and `test/zram` (the zram service against a fake root with stubbed tools) and `test/rotd` (glassine-rotd's decisions by replay, then the daemon against a stand-in IIO accelerometer and headless sway: turns, a paused stream, fixed turns, a new sway) and `test/predict` (glassine-predict with a model built from a small corpus: ranking, the contract with malformed, overlong and stale requests and random bytes, one private-field check per purpose and hint, learning, removing, putting back, erasing, the words command and the file modes). None of them need a device. What they can't show: real zram, writeback and recompression, `glassine-zpark` finding real zram slots, the spill on a device's userdata, a real IIO buffer and a device's axes.

## Installing

The programs from `make` go to `/usr/bin` (`glassine-shell`, `glassine-swayd`, `glassine-sleep-pill`, `glassine-predict`) and `/usr/sbin` (`glassine-bgprio`, `glassine-zpark`, `glassine-rotd`), the GTK module to `/usr/lib/gtk-3.0/modules/libglassine-ink.so`; everything under `rootfs/` goes to the same path on the target, and the models from `make predict-model` (`LANG.gpm` with `LANG.gpm.license`) to `/usr/share/glassine/predict/`. A package or image adds `glassine-zram` and, on a device with an accelerometer, `glassine-rotd` to a runlevel (`rc-update add glassine-zram default`); `glassine-bgprio` needs no service, the session starts it. Their kernel needs: cgroup v2 with the cpu controller (the freezer and `uclamp` are used when present), `process_madvise`, zram with writeback (`CONFIG_ZRAM_WRITEBACK`; `writeback_after` and `glassine-zram writeback` also need `CONFIG_ZRAM_TRACK_ENTRY_ACTIME` for the idle age), a second compression algorithm (`CONFIG_ZRAM_MULTI_COMP`) and, for the per-app recompression of a park without a spill, a zram `recompress` that takes `page_index=` / `page_indexes=` (without it such a park reports the failed step); the spill needs `btrfs-progs` and loop devices, else it falls back to a plain file.

`/etc/glassine/zram` (shell `KEY=value`, all optional; a device ships its own values):

| Key | Default | |
|---|---|---|
| `ZRAM_SIZE` | the RAM size | uncompressed swap size, RAM + disk |
| `ZRAM_MEM_LIMIT` | a quarter of the RAM | compressed RAM zram may use |
| `ZRAM_ALGO` | `lz4` | first algorithm (fast to decompress) |
| `RECOMP_ALGO`, `RECOMP_LEVEL` | `zstd`, `3` | per-app recompression in RAM, for a park without a spill; level 0 = none |
| `WB_SIZE_MB` | `4096` | logical spill size; 0 = no spill |
| `SPILL_MB` | `2048` | disk space of the btrfs image |
| `SPILL_LEVEL` | `9` | btrfs `compress-force=zstd` level |
| `SPILL_DIR` | `<USERDATA_MNT>/<USERDATA_PREFIX>/zram` | where the spill files live |
| `USERDATA_RESERVE_MB` | `4096` | free space the spill never takes |
| `SWAPPINESS` | `20` | `vm.swappiness` while zram is on |

`/etc/glassine/rotd.conf` (`KEY=value`, `#` comments, all optional; a device ships its own): `SENSOR` (the accelerometer's IIO `name`; default the first IIO device with an `in_accel_x` scan element), `OUTPUT` (the sway output; default `*`), `AXES` (two signed sensor axes, `[+-]x|y|z`, that read +g when the screen's right edge, then its bottom edge, points down; default `-x y`), `RATE_HZ` (4), `HOLD_MS` (500, how long an orientation must hold). The setting `/etc/glassine/rotation` is `auto`, `lock N` (N = 0, 90, 180, 270) or `lock`; the applied turn is in `/run/glassine-rotation`. While it streams, a power daemon may disable the sensor's IIO buffer for a suspend and enable it after; glassine-rotd waits that out and turns the stream off itself in lock mode and when it stops. The full description is the header of `src/glassine-rotd.c`.

`/etc/glassine/bgprio.conf` (`#` comments, re-read every 30 s): `NAME...` (never demoted), `nofreeze NAME...`, `freeze NAME...` (the settings app writes these two), `freeze_after` (120 s; 0 = never), `reclaim swap|always|off`, `park_after` (600 s; 0 = never), `writeback_after` (0 = off), `bg_uclamp_max` (79 %). The full description is the header of `src/glassine-bgprio.c`.

To turn the spill off on a device: `rc-service glassine-zram stop`, then `WB_SIZE_MB=0` in `/etc/glassine/zram` (or remove the service from its runlevel) and delete `<USERDATA_PREFIX>/zram/` on userdata. Nothing else on userdata is touched. If the stop fails (`swapoff` can't fit the swapped pages back into RAM), zram and its spill stay on: close apps and stop again, or set `WB_SIZE_MB=0`, reboot, and delete the directory after that. Deleting it while the spill is attached frees nothing until the next boot.

## Runtime interfaces

Other repositories use these, so renaming one means changing them in the same release:

- `/run/glassine-epdd-desktop.ctl`, `/run/glassine-epdd-desktop.classes`, `/run/glassine-rotation`, `/run/glassine-light`, `/run/glassine-bgprio.sock`
- `glassine-sleep-pill [-t VOICE] [-r DEG] [-s TEXT] [-W W -H H] [-p PNG] OUT` (spec 004; the power daemon's call: `glassine-sleep-pill /run/glassine-sleep-pill.pam`): PAM `GRAYSCALE_ALPHA`, premultiplied, cropped, with `# at X Y` in the panel daemon's logical px; exit 1 and no file when it can't draw. Reads `/etc/glassine/epd.conf` (`$GLASSINE_EPD_CONF`; `panel_width`, `panel_height`, `rotation`), `/run/glassine-rotation` and the shell's theme file
- the GTK module name `glassine-ink` (the session's `GTK_MODULES`), `GLASSINE_INK_WIDGETS`, `GLASSINE_INK_LOG=1` (spec 003)
- `/etc/glassine/*`: `eink.conf`, `rotation`, `rotd.conf`, `power.conf`, `launch-boost`, `grid-hidden`, `bgprio.conf`, `fg-boost`, `zram`, `home-app` (the home app's app_id, default `glassine-home`; or `$GLASSINE_HOME_APP_ID`; glassine-swayd, glassine-shell and `glassine-home-start` read it the same way, and the session starts it from `ID.desktop`, else a command of that name) and the session's renderer flags. The settings among them that an update carries into the next deployment are listed in `/usr/share/glassine/migrate.d/glassine-shell.list` (GlassineOS/boot, docs/deployments.md, "Migrate lists")
- the OpenRC service `glassine-rotd`
- the OpenRC service `glassine-zram`, its spill directory `<USERDATA_PREFIX>/zram/` on userdata, and `glassine-zram park|writeback|status`
- `$XDG_RUNTIME_DIR/glassine-swayd`, `~/.config/glassine-shell/theme`
- `glassine-swayd VERB ARGS` (one datagram on `$XDG_RUNTIME_DIR/glassine-swayd`; window ids in hex): `place M T up|down|left|right` (also on M's own screen), `newscreen M FOLLOW`, `swaphalves W`, `swapin W`, `swap A B`, `cross W`, `direction W`, `orient L S`, `turn W`, `full [W]` (no W: the focused window), `swapdir D`, `scrmove N -1|1`, `scrclose N`, `home show|hide|toggle`, `next new|here|hole`, `apps?`
- glassine-swayd to glassine-shell (`$XDG_RUNTIME_DIR/glassine-shell`): `dividers N,ORIENT0 D...` and `divider-tap ...`, each divider `x,y,w,h,vert,level,a,b,pos` (level 0 the main line, 1 and 2 the inner line of half 0 and 1; pos its position as a fraction); `home VISIBLE HOLE` (HOLE: the empty screen's number under Home, 0 none)
- `glassine-predict` (spec 006): the predictor line contract version 2 on stdin/stdout (version 1 still answered), the learned words store `$XDG_DATA_HOME/glassine/predict/words` (version 1), `$XDG_CONFIG_HOME/glassine/predict.conf` (`learning`, `addresses`, `languages`), `glassine-predict words list|removed|remove|restore|erase`, the models in `/usr/share/glassine/predict/LANG.gpm` (`$GLASSINE_PREDICT_MODELS`); all in `docs/predictor.md`
- `glassine-shell`, started by the sway config through `glassine-shell-run`
- `glassine-shell launch ID new|here [ARG]` (Home's taps; spec 001 FR-045 to FR-048): ARG an absolute path of an existing regular file, given to the app where its Exec line has `%f %F %u %U` (else appended, never through a shell), or for `glassine-settings` the name of a page Settings opens on (its page argument, e.g. `updates`, `power#2`); anything else is refused and logged. `home` (Home shown or hidden), `toggle overview|quick|notices|power` (`toggle launcher` and `toggle workspaces` still work, as `home` and `toggle overview`)
- `~/.config/glassine/gestures` (Settings writes, sway reads): `FINGERS DIRECTION ACTION` and `2 tap ACTION`; actions `nothing refresh screen-next screen-prev move-next move-prev overview home minimize fullscreen unfullscreen swap close quick notices page-up page-down` (`right-click` for the tap); Settings reads the old `ws-next ws-prev launcher maximize` as `screen-next screen-prev home fullscreen` and never writes them
- `~/.config/glassine/home.conf` (GlassineOS/home's layout file): Settings edits only its `favourites`, under `home.conf.lock`; the old `~/.config/glassine-shell/favourites` is only imported, when there is no `home.conf`

## Licence

GPL-2.0-or-later (`LICENSE`). The icons in `rootfs/usr/share/icons/glassine-mono` are CC BY-SA 4.0 (their own `LICENSE`). The word models built by `make predict-model` are not GPL: each carries its own licence and attributions (`LANG.gpm.license`; English and German: CC BY-SA 3.0, from Wikipedia and Tatoeba). The wayland protocols in `src/wayland/protocols` keep the licences in their headers (`glassine-ink-v1.xml`: GPL-2.0-or-later, as in GlassineOS/protocols).
