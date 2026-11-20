# Feature Specification: The shutdown screen

**Feature Branch**: `shutdown-screen`
**Status**: Draft
**Input**: "I want an overlay screen when the device is restarting or powering down: the
GlassineOS logo with a line of text beneath it. Even a soft restart is irritating if the
e-ink device sits there for 15 seconds without responding to input."
**Builds on**: GlassineOS/boot [011 boot splash](https://github.com/GlassineOS/boot/blob/main/specs/011-boot-splash/spec.md)
(the one-time override, "leave the screen"), [013 soft restart](https://github.com/GlassineOS/boot/blob/main/specs/013-soft-restart/spec.md)
(FR-031, the restart picture), this repository's 004 (the sleep pill) and 008 (the restart
entries).

## Context

Stopping a session takes seconds: the apps close, the services stop, the file systems are
made read-only. On an e-ink panel the last page stays on screen all that time and after
the power is gone, so a device that is restarting or switched off looks like one that
hangs. On a soft restart the kernel even keeps running, and nothing on screen changes until
the next session draws.

The shutdown screen replaces the page as soon as a restart or a power-off starts: the
GlassineOS mark, where the boot splash has it, with one line beneath it that says what
happens. It is drawn once, as a whole-panel refresh, and nothing draws over it until the
next start draws (or never, after a power-off).

## Terms

| Term | Meaning |
|---|---|
| shutdown | every end of the running session: a power-off, a restart (soft or cold), a restart into the recovery, the loader's start menu or the device's other system |
| target | what the shutdown leads to: `restart`, `poweroff`, `recovery`, `menu`, `other` (the device's other system), or unknown |
| the shutdown record | the file in which the system's restart and power-off tools say the target before they start a shutdown (GlassineOS/boot, `docs/deployments.md`, "Runtime directory") |
| panel daemon | the program that owns an e-ink panel while the session runs (GlassineOS/epd, `glassine-epdd-desktop`) |
| bistable display | a display that keeps its last image without power (e-ink) |

## User scenarios

### Scenario 1 – Restart from the power menu (Priority: P1)

**Given** the session on an e-ink device,
**When** the person taps Restart, Restart fully, or a "Restart into" entry,
**Then** within a second the panel shows the mark with "Restarting…" (or "Starting
recovery…", "Starting Android…" for the device's other system), in one whole-panel
refresh, and it stays until the next start draws. Touches and the pen change nothing on the
panel from then on.

### Scenario 2 – Power-off (Priority: P1)

**Given** the session on an e-ink device,
**When** the person taps Power off, or runs `poweroff` in a terminal,
**Then** within a second the panel shows the mark with "Powered off", and it stays on the
panel while the device is off. The next power-on draws the boot splash over it.

### Scenario 3 – A shutdown that doesn't come from the shell (Priority: P1)

**Given** the session on an e-ink device,
**When** a shutdown starts any other way (`reboot` or `poweroff` in a terminal,
`glassine-deploy restart`, `select --restart`, an update's restart, init asked by
another program),
**Then** the same screen appears as the session's stop begins, worded by the target the
shutdown record names, and "Shutting down…" where none is named.

### Scenario 4 – The screen survives the next start (Priority: P1)

**Given** a restart into GlassineOS, the recovery or the start menu on a bistable display,
**When** the device starts again,
**Then** no stage before the next system's first frame clears the shutdown screen or
draws the boot splash over it: the shutdown leaves the instruction "leave the screen" as
the next start's one-time override (011 FR-014). After a power-off, a soft restart and a
start of the device's other system, no override is written.

### Scenario 5 – Machines without an e-ink panel (Priority: P2)

**Given** a machine without a panel daemon (a PC),
**When** a shutdown starts,
**Then** nothing is drawn, nothing is written to the splash store, and the shutdown is
neither slower nor different from one without this feature.

## Requirements

### What is drawn

- **FR-001** The picture fills the logical screen of the panel daemon: a white page, the
  GlassineOS mark of the boot splash (the same shapes, the same size, the same place in
  the person's view: centred, its centre at 44 % of the height, 41 % of the narrow side
  wide), and one line of text centred beneath it in the voice's heading font, in its ink.
- **FR-002** The picture turns with the screen as the sleep pill does (004 FR-003): upright
  for the person who holds the device.
- **FR-003** The words, by target, are kept in one table of the shell's shutdown command,
  so they can be changed in one place:

  | Target | Line |
  |---|---|
  | `restart` | Restarting… |
  | `menu` | Restarting… |
  | `recovery` | Starting recovery… |
  | `other` | Starting Android… (the device's other system, as the power menu names it) |
  | `poweroff` | Powered off |
  | unknown | Shutting down… |

- **FR-004** The renderer is the sleep pill's (`glassine-sleep-pill -m screen -s TEXT …
  OUT.pgm`): the same voice, rotation and screen size rules (004 FR-002 to FR-005); the
  output is a binary 8-bit PGM (P5) of the whole logical screen, written to `OUT.tmp` and
  renamed.

### Who draws, and when

- **FR-005** One command, `glassine-shutdown-screen [TARGET]`, shows the screen. Without
  a target it reads the shutdown record, then the soft restart's record (a restart), then
  the shell's own power-off mark; else the target is unknown. It renders the picture to
  `/run/glassine-shutdown-screen.pgm` and asks the panel daemon to show it
  (`shutdown-screen` on its control FIFO). It never waits more than five seconds, never
  fails a shutdown (exit 0 whatever happens) and never blocks on a FIFO no daemon reads.
- **FR-006** It shows the screen once per kernel run's session: a second call (any
  target) does nothing. The first call records the target it drew in
  `/run/glassine-shutdown-screen`.
- **FR-007** The power menu's root helper (`glassine-power-root`) runs it first, before
  anything that can take time, for `poweroff`, `reboot`, `reboot-full`, `android`
  (`other`), `recovery` and `menu`. A request that is refused before the shutdown starts
  (the start menu on a loader that takes no request, no second system) shows nothing.
- **FR-008** The session's service runs it as its stop begins, when the system goes down
  (not on a plain restart of the service), before it ends the session: that covers every
  shutdown that doesn't come through the power menu (Scenario 3).
- **FR-009** The panel daemon, on `shutdown-screen`, puts the picture on the whole panel
  as one flashing GC16 refresh, and from then on writes nothing more to the panel until it
  exits: no compositor frame, no pen preview, no cleanup, no sleep banner, no wake, no
  resume, no full refresh. It exits as before (SIGTERM: running updates finished, rails
  down), with nothing drawn on the way out. A picture it can't read (missing, not a P5 of
  the logical screen's size) gives a white page, with the line's capitals as its bar when
  the command carries them.
- **FR-010** A device's own power-off screen, drawn after the panel daemon has gone, is not
  drawn when the shutdown screen was shown (`/run/glassine-shutdown-screen` exists).

### Keeping it through the next start

- **FR-011** For the targets `restart`, `menu` and `recovery`, once the panel daemon was
  asked to show the screen, the command leaves `/run/glassine/screen-keep`. The system's
  cold shutdown step turns that into the splash store's "leave the screen" override
  (GlassineOS/boot 011 FR-014); a soft restart writes none (013 FR-032). For `poweroff`,
  `other` and unknown it leaves no such file.

## Contracts

| Contract | Owned / consumed | Spec location |
|---|---|---|
| `glassine-shutdown-screen [restart\|poweroff\|recovery\|menu\|other]` | owned (new) | README, "Runtime interfaces" |
| `glassine-sleep-pill -m screen` and its P5 output | owned (added mode) | README, "Runtime interfaces" |
| `/run/glassine-shutdown-screen.pgm`, `/run/glassine-shutdown-screen` | owned (new) | README, "Runtime interfaces" |
| `/run/glassine/screen-keep` | owned (new); read by GlassineOS/boot's cold shutdown step | boot `docs/deployments.md`, "Runtime directory" |
| the panel daemon's `shutdown-screen` control line | consumed (GlassineOS/epd) | epd `docs/control-fifo.md` |
| the shutdown record `/run/glassine/shutdown` (`next=`) | consumed (GlassineOS/boot) | boot `docs/deployments.md`, "Runtime directory" |

## Edge cases

- **The panel daemon is gone or hung:** the FIFO write is bounded; nothing is drawn, no
  `screen-keep` is left, the shutdown goes on. A device's own power-off screen
  (FR-010) still draws, since nothing was shown.
- **The device sleeps when the shutdown starts** (a power-off from the power key's long
  press, a low battery): the panel daemon leaves its sleep state for the screen.
- **No usable fonts, a renderer that fails:** the daemon's fallback page (FR-009).
- **A restart the step turns cold after all** (013 FR-021): the screen already says
  "Restarting…", which stays true; the next session says why (013 FR-007).
- **A shutdown that hangs:** the screen stays; the device's watchdog or the person ends it.
- **A rotated screen:** FR-002. The boot splash that follows a power-off is portrait.

## Success criteria

- **SC-001** In the shell's host test, `glassine-sleep-pill -m screen` gives a P5 of the
  logical screen in every voice and turn, with the mark where the boot splash has it and
  the line beneath it.
- **SC-002** In the host test, `glassine-shutdown-screen` with a stand-in panel daemon:
  the FIFO gets one `shutdown-screen` line, a second call sends nothing, every target gets
  its words, `screen-keep` exists exactly for `restart`, `menu` and `recovery`, a FIFO
  nobody reads costs at most five seconds and exits 0.
- **SC-003** The root helper's host test: every shutdown verb runs the command before
  the rest; a refused request runs it not.
- **SC-004** In GlassineOS/epd's host test, after `shutdown-screen` the policy sends one
  full-panel GC16 and nothing more for any later input or command.
- **SC-005** On the device, with the person's go: from the power menu and from a terminal,
  restart and power-off show the screen within a second; a cold restart keeps it until
  the boot's first panel update; a power-off keeps it while off.

## Out of scope

- The boot splash itself and what the boot draws after the screen (011).
- A progress indicator, a second line, an animation.
- A shutdown screen on PCs (a framebuffer or KMS picture after the compositor has gone).
