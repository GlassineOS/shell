# Feature Specification: The sleep pill

**Feature Branch**: `ink-module-sleep-pill`
**Status**: Draft
**Input**: "The power daemon runs `glassine-sleep-pill` before suspend, and the panel
daemon draws its output over the last page. Provide the renderer, in the shell's look."

## Context

When the device sleeps, the last page stays on the e-ink panel. A small rounded pill near
the bottom edge of what the person sees says that the device is asleep: a moon in a ring,
"Sleeping", "Press the power key to wake" and the time sleep started ("Since 20:11").

The device's power daemon runs `timeout 3 /usr/bin/glassine-sleep-pill
/run/glassine-sleep-pill.pam` as root before it asks the panel daemon to sleep.
`glassine-epdd-desktop` reads that file and draws it over the page; when the file is
missing, unreadable or older than 60 s, it draws its plain bar instead.

## User scenarios

### Scenario 1 – The pill matches the shell (Priority: P1)

**Given** the shell in any of its three voices,
**When** the device goes to sleep,
**Then** the pill is drawn in that voice (its card, ink and secondary colours and fonts).

### Scenario 2 – The pill turns with the screen (Priority: P1)

**Given** the screen turned to any of the four orientations,
**When** the device goes to sleep,
**Then** the pill sits centred at the bottom edge of what the person sees, upright for them.

### Scenario 3 – A failure falls back to the bar (Priority: P2)

**Given** a renderer that cannot run (no screen size, nothing drawn, unwritable output),
**When** the device goes to sleep,
**Then** the renderer exits non-zero, leaves no partial file and the panel daemon
draws its bar.

## Requirements

- **FR-001**: `glassine-sleep-pill [-t VOICE] [-r DEG] [-s TEXT] [-W W -H H] [-p PNG] OUT`.
- **FR-002**: The voice: `-t`, else `$HOME/.config/glassine-shell/theme`, else the same file of
  the first account with a uid from 1000 (the power daemon runs as root), else White.
- **FR-003**: The rotation: `-r`, else `/run/glassine-rotation` (the clockwise turn of the
  output, as `glassine-shell` reads it), else 0.
- **FR-004**: The logical screen (portrait, transform 0, the panel daemon's frame): `-W/-H`,
  else from the device's panel adapter `/etc/glassine/epd.conf` (`$GLASSINE_EPD_CONF`):
  `panel_width` x `panel_height`, swapped when `rotation` is `cw` or `ccw` (the default).
  No size: exit 1.
- **FR-005**: The design is drawn at 702 px on the short side and scaled by
  round(short side / 702), as the shell does.
- **FR-006**: The time text: `-s`, else "Since HH:MM", local time now.
- **FR-007**: Output: PAM `P7`, `DEPTH 2`, `MAXVAL 255`, `TUPLTYPE GRAYSCALE_ALPHA`, a
  comment line `# at X Y` with the crop's place in logical px, then the pixels cropped to
  everything the pill touched. Grey is premultiplied (grey ≤ alpha). Written to `OUT.tmp`
  and renamed.
- **FR-008**: Fast enough for the power daemon's 3 s timeout on the device, as root with a
  minimal environment.

## Acceptance

Host test (`test/sleep-pill`): for the three voices and the four rotations the output is
accepted by the same checks as `glassine-epdd-desktop`'s reader (header, comment, in the
screen, payload length), premultiplied, and the crop sits at the bottom edge of the
turned view; the screen size comes from an `epd.conf`; without one the renderer fails and
leaves no file.
