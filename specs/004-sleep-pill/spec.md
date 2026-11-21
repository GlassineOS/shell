# Feature Specification: The sleep pill

**Feature Branch**: `ink-module-sleep-pill`
**Status**: Draft
**Input**: "The power daemon runs `glassine-sleep-pill` before suspend, and the panel
daemon draws its output over the last page. Provide the renderer, in the shell's look."

## Context

When the device sleeps, the last page stays on the e-ink panel. A small rounded pill near
the bottom edge of what the person sees says that the device is asleep: a moon in a ring,
"Sleeping", "Press the power key to wake" and the time sleep started ("Since 20:11").
The same pill says "Hibernating" when the device goes down with its session kept, and,
over the picture the next start shows (010's start picture), that the device is on its
way back: "Waking up…", "Back in a moment", no time. Between these modes only the words
change: the voice, the box, the ring and its moon, the place stay.

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

- **FR-001**: `glassine-sleep-pill [-m MODE] [-t VOICE] [-r DEG] [-s TEXT] [-W W -H H | -b PNM] [-p PNG] OUT`.
- **FR-001a**: The mode: `sleep` (the default), `hibernate` or `resume`. Each mode's words
  (title, hint, and whether the time shows) are written in one table in the renderer and
  nowhere else. sleep: "Sleeping", "Press the power key to wake", the time; hibernate:
  "Hibernating", "Press the power key to resume", the time; resume: "Waking up…", "Back in a
  moment", no time. Every mode draws the same box for the same screen, turn and voice.
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
- **FR-006**: The time text: `-s`, else "Since HH:MM", local time now; none in a mode
  without the time. An empty text draws none.
- **FR-007**: Output: PAM `P7`, `DEPTH 2`, `MAXVAL 255`, `TUPLTYPE GRAYSCALE_ALPHA`, a
  comment line `# at X Y` with the crop's place in logical px, then the pixels cropped to
  everything the pill touched. Grey is premultiplied (grey ≤ alpha). Written to `OUT.tmp`
  and renamed.
- **FR-007a**: Over a picture (`-b PNM`, a binary P5 or P6, the screen upright as the
  person saw it): the logical screen is that picture turned back by the rotation (its
  width and height swapped for 90 and 270); the picture is laid under the pill pixel for
  pixel, and OUT is the whole logical screen with the pill, as a binary PNM of the
  picture's kind (P5 grey, P6 RGB), written to `OUT.tmp` and renamed. `-W/-H` and the panel
  adapter are not read. A picture it can't read: exit 1, no file.
- **FR-008**: Fast enough for the power daemon's 3 s timeout on the device, as root with a
  minimal environment.

## Acceptance

Host test (`test/sleep-pill`): for the three voices and the four rotations the output is
accepted by the same checks as `glassine-epdd-desktop`'s reader (header, comment, in the
screen, payload length), premultiplied, and the crop sits at the bottom edge of the
turned view; the screen size comes from an `epd.conf`; without one the renderer fails and
leaves no file. The resume mode's box equals the hibernate mode's for every voice and
turn; over a picture, grey and RGB and all four turns, the output is the picture turned
back, exact outside the pill.
