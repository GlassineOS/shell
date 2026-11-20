# Feature Specification: The shutdown screen

**Feature Branch**: `shutdown-screen`
**Status**: Draft
**Input**: "I want an overlay screen when the device is restarting or powering down: the
GlassineOS logo with a line of text beneath it. Even a soft restart is irritating if the
e-ink device sits there for 15 seconds without responding to input."
**Builds on**: GlassineOS/boot [011 boot splash](https://github.com/GlassineOS/boot/blob/main/specs/011-boot-splash/spec.md)
(the one-time override, "leave the screen"), [013 soft restart](https://github.com/GlassineOS/boot/blob/main/specs/013-soft-restart/spec.md)
(FR-031, the restart picture), this repository's 001 (the shell and its surfaces) and 008
(the restart entries).

## Context

Stopping a session takes seconds: the apps close, the services stop, the file systems are
made read-only. Meanwhile the last page stays on screen and answers no input, so a machine
that is restarting or switching off looks like one that hangs. On an e-ink panel the page
even stays after the power is gone; on a soft restart the kernel keeps running and nothing
on screen changes until the next session draws.

The shutdown screen covers the session as soon as a restart or a power-off starts: the
GlassineOS mark, where the boot splash has it, with one line beneath it that says what
happens. The shell draws it as an ordinary full-screen surface above everything, on every
kind of display. Where the display keeps its picture without power, the screen is then
held: it is refreshed once in full, and the display takes no further change until the
next start draws.

## Terms

| Term | Meaning |
|---|---|
| shutdown | every end of the running session: a power-off, a restart (soft or cold), a restart into the recovery, the loader's start menu or the machine's other system |
| target | what the shutdown leads to: `restart`, `poweroff`, `recovery`, `menu`, `other` (the machine's other system), `bootloader`, or unknown |
| the shutdown record | the file in which the system's restart and power-off tools say the target before they start a shutdown (GlassineOS/boot, `docs/deployments.md`, "Runtime directory") |
| the overlay | the shutdown screen as the shell shows it: one surface the size of the screen, above every window, panel and popup |
| holding the screen | on a display that keeps its picture (bistable, e-ink): one full refresh of what is shown, then no further change reaches the display until the next start. On other displays it does nothing |
| panel control | the e-ink panel daemon's control line (GlassineOS/epd, `docs/control-fifo.md`): present only where such a daemon runs |
| suspend | the machine's sleep or hibernation, whoever asks for it (the power key, a cover, idle time, the power menu) |

## User scenarios

### Scenario 1 – Restart from the power menu (Priority: P1)

**Given** the session,
**When** the person taps Restart, Restart fully, or a "Restart into" entry,
**Then** within a second the screen shows the mark with "Restarting…" (or "Starting
recovery…", "Starting Android…" for the machine's other system), covering everything.
Touches, keys and the pen change nothing on screen from then on. On an e-ink panel the
picture is refreshed once in full and stays until the next start draws.

### Scenario 2 – Power-off (Priority: P1)

**Given** the session,
**When** the person taps Power off, or runs `poweroff` in a terminal,
**Then** within a second the screen shows the mark with "Powered off". On an e-ink panel it
stays on the panel while the machine is off; the next power-on draws the boot splash over
it.

### Scenario 3 – A shutdown that doesn't come from the shell (Priority: P1)

**Given** the session,
**When** a shutdown starts any other way (`reboot` or `poweroff` in a terminal,
`glassine-deploy restart`, `select --restart`, an update's restart, init asked by
another program),
**Then** the same screen appears as the session's stop begins, worded by the target the
shutdown record names, and "Shutting down…" where none is named.

### Scenario 4 – The screen survives the next start (Priority: P1)

**Given** a restart into GlassineOS, the recovery or the start menu on a display that keeps
its picture,
**When** the machine starts again,
**Then** no stage before the next system's first frame clears the shutdown screen or draws
the boot splash over it: the shutdown leaves the instruction "leave the screen" as the
next start's one-time override (011 FR-014). After a power-off, a soft restart and a start
of the machine's other system, no override is written; on a display that doesn't keep its
picture, none is written either.

### Scenario 5 – No suspend once the shutdown started (Priority: P1)

**Given** a shutdown that has started,
**When** the person presses the power key, closes the cover, or the machine is idle,
**Then** the machine doesn't sleep or hibernate: the shutdown goes on and the screen stays
as it is.

### Scenario 6 – Ordinary displays (Priority: P1)

**Given** a machine whose display doesn't keep its picture (a PC's screen),
**When** a shutdown starts,
**Then** the overlay shows exactly as on an e-ink device, until the compositor ends;
holding the screen does nothing, and nothing is written to the splash store.

## Requirements

### What is drawn

- **FR-001** The overlay fills the screen: a white page, the GlassineOS mark of the boot
  splash (the same shapes and greys, in the person's view: centred, its centre at 44 % of
  the height, 41 % of the narrow side wide), and one line of text centred beneath it in
  the voice's heading font, in its ink.
- **FR-002** It is upright for the person: drawn in the screen's current orientation, as
  every other surface of the shell.
- **FR-003** The words, by target, are kept in one table of the shutdown command, so they
  can be changed in one place:

  | Target | Line |
  |---|---|
  | `restart` | Restarting… |
  | `menu` | Restarting… |
  | `bootloader` | Restarting… |
  | `recovery` | Starting recovery… |
  | `other` | Starting Android… (the machine's other system, as the power menu names it) |
  | `poweroff` | Powered off |
  | unknown | Shutting down… |

- **FR-004** The overlay lies above every other surface (windows, the bar, popups, the
  on-screen keyboard, an immersive app) and takes all input: taps, keys and pen input on it
  do nothing. Any open popup closes and the on-screen keyboard hides when it appears. Once
  shown, the shell takes no further request and draws nothing else.

### Who draws, and when

- **FR-005** One command, `glassine-shutdown-screen [TARGET]`, run by the system, shows
  the screen. Without a target it reads the shutdown record, then the soft restart's
  record (a restart), then the power menu's power-off mark; else the target is unknown.
  It asks the running session's shell to show the overlay with the target's line, and
  waits for the shell's answer for at most three seconds. It never waits more than five
  seconds in all, never fails a shutdown (exit 0 whatever happens) and does nothing
  where no session's shell runs.
- **FR-006** It acts once per start of the system (a soft restart is a new start: its runtime files are new): a second call (any target) does nothing. Before
  it asks the shell, it marks the shutdown as started (FR-013).
- **FR-007** The power menu's root helper (`glassine-power-root`) runs it first, before
  anything that can take time, for `poweroff`, `reboot` and `reboot-full` (`restart`), and
  for `android` (`other`) once the slot switch is written. It is never shown for a request
  that can still be refused before the shutdown starts: `recovery` and `menu` (their
  requests may be refused) are shown by FR-008 once the shutdown has started, worded by
  the shutdown record; a refused `android` (no second system, a failed switch) shows
  nothing.
- **FR-008** The session's service runs it as its stop begins, when the system goes down
  (not on a plain restart of the service), before it ends the session: that covers every
  shutdown that doesn't come through the power menu (Scenario 3).
- **FR-009** The shell, asked to show the screen, shows the overlay; once the compositor
  reports the overlay's first frame on screen, it holds the screen (FR-010). It then
  answers in its runtime directory whether the screen is held (`held`) or only shown
  (`shown`: holding did nothing, or the frame was not reported in time). It holds nothing
  when the frame isn't reported within a bound, so a late overlay can still reach the
  display.

### Holding the screen

- **FR-010** Holding is one step of the shell's common code with no knowledge of a
  particular device: where the panel control exists, it waits a short settle time, asks
  for one full refresh, waits a short time for that refresh to start, then pauses the
  panel; where it doesn't exist, it does nothing. The waits are named constants. It uses
  only the panel daemon's existing generic lines (`full`, `pause`). A paused panel
  finishes the refresh it started and then takes no compositor frame, pen stroke or
  cleanup until the daemon ends.
- **FR-011** Only when the shell answered `held` does the command record the target in
  `/run/glassine-shutdown-screen` (a device's own power-off screen, drawn after the panel
  daemon has gone, then skips itself), and, for the targets `restart`, `menu` and
  `recovery` (the next start is the loader's), leave `/run/glassine/screen-keep`. The
  system's cold shutdown step turns that into the splash store's "leave the screen"
  override (GlassineOS/boot 011 FR-014); a soft restart writes none (013 FR-032). For
  `poweroff`, `other`, `bootloader` and unknown, and for an answer `shown` or none, it
  leaves no `screen-keep`.
- **FR-012** A device's own power-off screen is not drawn when the shutdown screen was held
  (`/run/glassine-shutdown-screen` exists).

### No suspend

- **FR-013** From the moment a shutdown has started, nothing suspends the machine: the
  shutdown command's own mark (`/run/glassine-shutdown-screen.lock`, made before it asks
  the shell) and the shutdown record (`/run/glassine/shutdown`) each mean "a shutdown has
  started". A power daemon refuses every suspend while either exists, whatever asked for
  it, and says so in its log.

## Contracts

| Contract | Owned / consumed | Spec location |
|---|---|---|
| `glassine-shutdown-screen [restart\|poweroff\|recovery\|menu\|other\|bootloader]` | owned | README, "Runtime interfaces" |
| the shell's request `shutdown-screen LINE` and its answer `$XDG_RUNTIME_DIR/glassine-shutdown-screen.shown` (`held` or `shown`) | owned | README, "Runtime interfaces" |
| `/run/glassine-shutdown-screen`, `/run/glassine-shutdown-screen.lock` | owned; the lock is read by power daemons (FR-013), the record by a device's power-off screen (FR-012) | README, "Runtime interfaces" |
| `/run/glassine/screen-keep` | owned; read by GlassineOS/boot's cold shutdown step | boot `docs/deployments.md`, "Runtime directory" |
| the panel daemon's `full` and `pause` lines | consumed (GlassineOS/epd) | epd `docs/control-fifo.md` |
| the shutdown record `/run/glassine/shutdown` (`next=`) | consumed (GlassineOS/boot) | boot `docs/deployments.md`, "Runtime directory" |

## Edge cases

- **No session, or a shell that doesn't answer:** nothing is shown, the command gives up
  after its bound, no `screen-keep` and no record are left, the shutdown goes on; a
  device's own power-off screen still draws.
- **The compositor doesn't report the frame in time:** the overlay stays mapped, nothing is
  held, the answer is `shown`.
- **No panel control (a PC), or a panel daemon that is gone:** the overlay alone; the
  answer is `shown`.
- **The machine sleeps when the shutdown starts** (a low battery's power-off during a
  sleep): the display keeps the sleeping page; the overlay reaches it only if the machine
  woke first.
- **A restart the step turns cold after all** (013 FR-021): the screen already says
  "Restarting…", which stays true; the next session says why (013 FR-007).
- **A shutdown that hangs:** the screen stays; the device's watchdog or the person ends it.
- **A rotated screen:** FR-002. The boot splash that follows a power-off is portrait.
- **After the compositor ends** (a display that doesn't keep its picture): what the machine
  shows until it is off is the system's console, outside this feature.

## Success criteria

- **SC-001** In the shell's host test under a headless compositor: the request shows a
  screen-sized overlay above windows, the bar and an open popup, with the mark where the
  boot splash has it and the line beneath it; taps and keys afterwards change nothing; the
  answer is `held` with a stand-in panel control (which received `full`, then `pause`,
  in separate writes, after the frame) and `shown` without one.
- **SC-002** In the host test of `glassine-shutdown-screen` with a stand-in shell: every
  target sends its words, a second call sends nothing, the lock exists before the
  request, `screen-keep` and the record exist exactly for a `held` answer (`screen-keep`
  only for `restart`, `menu` and `recovery`), a shell that doesn't answer costs at most
  five seconds and exits 0.
- **SC-003** The root helper's host test: `poweroff`, `reboot`, `reboot-full` and
  `android` run the command before the shutdown; a refused `android` runs it not.
- **SC-004** On the device, with the person's go: from the power menu and from a terminal,
  restart and power-off show the screen within a second; a cold restart keeps it until
  the boot's first panel update; a power-off keeps it while off; the power key and the
  cover during the shutdown don't suspend.

## Out of scope

- The boot splash itself and what the boot draws after the screen (011).
- A progress indicator, a second line, an animation.
- What a machine shows after its compositor has ended (its console).
