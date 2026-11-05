# GlassineOS/shell

The desktop around the apps on an e-ink device running sway:

- **`glassine-shell-wl`:** the bar, launcher, overview, quick settings, power view and notifications, drawn natively.
- **`glassine-swayd`:** the window model over sway's IPC: tiles, drag handles, dividers and when the on-screen keyboard shows.
- **`glassine-settings`:** the touch settings app (Python, GTK3, cairo), with its root helper.
- **`glassine-setup`:** the first-run setup, with its root helper.
- **The session:** `glassine-session`, `glassine-session-sway`, the sway config and key bindings, the on-screen keyboard wrappers and the `glassine-mono` icons.

**This repository isn't used for builds yet.** Images still come from the private lab tree, and this tree is exported from it. Until the cutover, a change made here has to go back to the lab by hand, or the next export overwrites it.

## Build and test

```
make            # glassine-shell-wl and glassine-swayd, on Alpine
make check      # compiles, then the host tests in Docker
```

`make check` runs `test/settings` (renders every page), `test/setup` (the first-run flow and its root helper) and `test/swayd` (the window model on stock sway, headless). None of them need a device.

## Runtime interfaces

Other repositories use these, so renaming one means changing them in the same release:

- `/run/glassine-epdd-desktop.ctl`, `/run/glassine-epdd-desktop.classes`, `/run/glassine-rotation`, `/run/glassine-light`, `/run/glassine-bgprio.sock`
- `/etc/glassine/*`: `eink.conf`, `rotation`, `power.conf`, `session`, `launch-boost`, `grid-hidden` and the session's renderer flags
- `$XDG_RUNTIME_DIR/glassine-swayd`, `~/.config/glassine-shell/theme`
- `glassine-shell-wl`, started by the sway config through `glassine-shell-wl-run`

## Licence

GPL-2.0-or-later (`LICENSE`). The icons in `rootfs/usr/share/icons/glassine-mono` are CC BY-SA 4.0 (their own `LICENSE`). The wayland protocols in `src/wayland/protocols` keep the licences in their headers.
