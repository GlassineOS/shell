# GlassineOS/shell

The desktop around the apps on an e-ink device running sway:

- **`glassine-shell-wl`:** the bar, launcher, overview, quick settings, power view and notifications, drawn natively.
- **`glassine-swayd`:** the window model over sway's IPC: tiles, drag handles, dividers and when the on-screen keyboard shows.
- **`glassine-settings`:** the touch settings app (Python, GTK3, cairo), with its root helper.
- **`glassine-setup`:** the first-run setup, with its root helper.
- **The session:** `glassine-session`, `glassine-session-sway`, the sway config and key bindings, the on-screen keyboard wrappers and the `glassine-mono` icons.
- **Background apps:** `glassine-bgprio` (started with the sway session) gives apps on hidden workspaces CPU time only when the apps in front don't want it, freezes them after a while (not media players, apps playing audio, Flatpaks with the background permission or the desktop itself) and pages their memory out; `glassine-zpark` moves one parked app's swapped memory to disk.
- **Compressed swap:** the OpenRC service `glassine-zram` (`/usr/sbin/glassine-zram start|stop|park|writeback|status`): zram swap whose parked apps spill to a compressed btrfs image on the user data partition, under `<USERDATA_PREFIX>/zram/` from `/etc/glassine/deploy.conf` (RAM only when userdata isn't mounted or short of space). Host-tested only: the freeze, page-out and spill chain hasn't run on a device from this repository.
- **Session services:** `glassine-pipewire` (PipeWire with the saved volume), `glassine-bt-trust` (trusts devices paired outside the settings), `glassine-ink-discover` (finds pen canvases over AT-SPI), `glassine-appenv` (per-app render profiles).
- **Themes:** the `glassine-white`, `glassine-editorial` and `glassine-stationery` GTK 3 themes and their libadwaita CSS (`usr/share/glassine/gtk4`, applied by `glassine-theme-apply`).
- **System files the session needs:** the doas and polkit rules for its helpers and Wi-Fi, `glassine-checkpass` (admin password check), the package-index refresh on connect and a boot health check.

**This repository isn't used for builds yet.** Images still come from the private lab tree, and this tree is exported from it. Until the cutover, a change made here has to go back to the lab by hand, or the next export overwrites it.

## Build and test

```
make            # glassine-shell-wl, glassine-swayd, glassine-bgprio and glassine-zpark, on Alpine
make check      # compiles, then the host tests in Docker
```

`make check` runs `test/settings` (renders every page), `test/setup` (the first-run flow and its root helper), `test/swayd` (the window model on stock sway, headless), `test/admin-auth` (the administrator tiers of the root helpers, with real accounts), `test/health` (the boot health check with BusyBox), `test/session-dispatch` (the session's restart loop), `test/bgprio` (glassine-bgprio on headless sway in a privileged container with its own cgroup v2 tree; the page-out check is skipped on a host without swap) and `test/zram` (the zram service against a fake root with stubbed tools). None of them need a device. What they can't show: real zram, writeback and recompression, `glassine-zpark` finding real zram slots, and the spill on a device's userdata.

## Installing

The programs from `make` go to `/usr/bin` (`glassine-shell-wl`, `glassine-swayd`) and `/usr/sbin` (`glassine-bgprio`, `glassine-zpark`); everything under `rootfs/` goes to the same path on the target. A package adds `glassine-zram` to a runlevel (`rc-update add glassine-zram default`); `glassine-bgprio` needs no service, the session starts it. Their kernel needs: cgroup v2 with the cpu controller (the freezer and `uclamp` are used when present), `process_madvise`, zram with writeback (`CONFIG_ZRAM_WRITEBACK`), a second compression algorithm (`CONFIG_ZRAM_MULTI_COMP`) and, for the per-app recompression, a zram `recompress` that takes `page_index=` / `page_indexes=` (without it a park reports the failed step and still writes back); the spill needs `btrfs-progs` and loop devices, else it falls back to a plain file.

`/etc/glassine/zram` (shell `KEY=value`, all optional; a device ships its own values):

| Key | Default | |
|---|---|---|
| `ZRAM_SIZE` | the RAM size | uncompressed swap size, RAM + disk |
| `ZRAM_MEM_LIMIT` | a quarter of the RAM | compressed RAM zram may use |
| `ZRAM_ALGO` | `lz4` | first algorithm (fast to decompress) |
| `RECOMP_ALGO`, `RECOMP_LEVEL` | `zstd`, `3` | per-app recompression before a park; level 0 = none |
| `WB_SIZE_MB` | `4096` | logical spill size; 0 = no spill |
| `SPILL_MB` | `2048` | disk space of the btrfs image |
| `SPILL_LEVEL` | `9` | btrfs `compress-force=zstd` level |
| `SPILL_DIR` | `<USERDATA_MNT>/<USERDATA_PREFIX>/zram` | where the spill files live |
| `USERDATA_RESERVE_MB` | `4096` | free space the spill never takes |
| `SWAPPINESS` | `20` | `vm.swappiness` while zram is on |

`/etc/glassine/bgprio.conf` (`#` comments, re-read every 30 s): `NAME...` (never demoted), `nofreeze NAME...`, `freeze NAME...` (the settings app writes these two), `freeze_after` (120 s; 0 = never), `reclaim swap|always|off`, `park_after` (600 s; 0 = never), `writeback_after` (0 = off), `bg_uclamp_max` (79 %). The full description is the header of `src/glassine-bgprio.c`.

To turn the spill off on a device: `rc-service glassine-zram stop`, then `WB_SIZE_MB=0` in `/etc/glassine/zram` (or remove the service from its runlevel) and delete `<USERDATA_PREFIX>/zram/` on userdata. Nothing else on userdata is touched. If the stop fails (`swapoff` can't fit the swapped pages back into RAM), zram and its spill stay on: close apps and stop again, or set `WB_SIZE_MB=0`, reboot, and delete the directory after that. Deleting it while the spill is attached frees nothing until the next boot.

## Runtime interfaces

Other repositories use these, so renaming one means changing them in the same release:

- `/run/glassine-epdd-desktop.ctl`, `/run/glassine-epdd-desktop.classes`, `/run/glassine-rotation`, `/run/glassine-light`, `/run/glassine-bgprio.sock`
- `/etc/glassine/*`: `eink.conf`, `rotation`, `power.conf`, `launch-boost`, `grid-hidden`, `bgprio.conf`, `fg-boost`, `zram` and the session's renderer flags
- the OpenRC service `glassine-zram`, its spill directory `<USERDATA_PREFIX>/zram/` on userdata, and `glassine-zram park|writeback|status`
- `$XDG_RUNTIME_DIR/glassine-swayd`, `~/.config/glassine-shell/theme`
- `glassine-shell-wl`, started by the sway config through `glassine-shell-wl-run`

## Licence

GPL-2.0-or-later (`LICENSE`). The icons in `rootfs/usr/share/icons/glassine-mono` are CC BY-SA 4.0 (their own `LICENSE`). The wayland protocols in `src/wayland/protocols` keep the licences in their headers.
