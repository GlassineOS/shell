# Feature Specification: The size of a display of unknown size

**Feature Branch**: `output-density`
**Status**: Draft
**Input**: "For an output whose physical size is unknown, the person sets its pixels per
millimetre once in Settings, and the value is kept for that output."
**Builds on**: GlassineOS/handwriting [004 handwriting for apps](https://github.com/GlassineOS/handwriting/blob/main/specs/004-handwriting-for-apps/spec.md)
FR-017 (the pen interface's units: the points per millimetre of the output the pen writes
on).

## Context

Handwriting judges writing by its physical size. The compositor knows how many
millimetres a display is when the display says so (a built-in panel, most monitors); some
displays never do (some projectors, adapters, virtual displays). For those, the person
measures the display once in Settings: a ruler drawn on the screen is matched to a real
ruler or a bank card, and the result is kept for that display. A display that reports its
size never shows the setting.

## Terms

| Term | Meaning |
|---|---|
| output | one display of the session, as the compositor names it |
| output key | the compositor's identifier of the output, its make, model and serial joined by single spaces; when all three are unknown, its connector name |
| size unknown | the compositor reports no physical width or height for the output (0 mm) |
| density | the output's pixels per millimetre: the pixels of its current mode, as the mode is (not turned, not scaled), over its width in millimetres |
| points per millimetre | the density divided by the output's scale: what the pen interface uses (FR-017) |

## User scenarios

### Scenario 1 – Measure a display of unknown size (Priority: P1)

**Given** a session with a display whose size is unknown and no density kept for it,
**When** the person opens Appearance,
**Then** a section "Screen size" lists that display by its name ("Not measured"), and a
tap opens its page: a ruler drawn across the page, its length in millimetres beside it,
buttons that make it longer and shorter, and the density as a number that can be typed.

**Given** that page, with Settings on that display,
**When** the person makes the ruler as long as a bank card's long edge (85.6 mm) or the
same length on a real ruler,
**Then** the density is kept for that display at once, and the display's row on
Appearance shows it.

### Scenario 2 – Type the number (Priority: P2)

**Given** the page of a display,
**When** the person types a density and confirms it,
**Then** it is kept for the display, and the ruler is redrawn at it.

### Scenario 3 – A display that reports its size (Priority: P1)

**Given** a session whose every display reports its size (a tablet's built-in panel),
**When** the person opens Appearance,
**Then** there is no "Screen size" section; the rest of the page is unchanged.

### Scenario 4 – Forget a measurement (Priority: P3)

**Given** a display with a kept density,
**When** the person taps "Forget" on its page,
**Then** the display's line is removed and the display counts as not measured again.

## Requirements

- **FR-001**: Settings lists the outputs whose size is unknown, from the compositor's
  outputs and the physical size the display server gives each one. An output with a kept
  density is listed too, whatever size it reports now. The section and its rows are absent
  when there is none.
- **FR-002**: The density is kept in `$XDG_CONFIG_HOME/glassine/output-density`
  (default `~/.config/glassine/output-density`): one line per output,
  `DENSITY OUTPUT-KEY`, the density a decimal number with a dot, then one space, then the
  output key to the end of the line. Lines starting with `#` and blank lines are
  comments. A line whose density is not a number from 1 to 100 is ignored. Of two lines
  for one key, the later counts.
- **FR-003**: A write keeps every other line of the file, replaces the output's line or
  adds one at the end, writes the density with at most three decimals, and replaces the
  file as a whole (written beside it, then renamed), so a reader never sees half a file.
  Forget removes the output's line.
- **FR-004**: The ruler is drawn in the window's logical pixels at the output's density
  divided by the output's scale, from the left edge of the page's content. Its length is a
  bank card's (85.6 mm) when that fits the page; otherwise the most whole centimetres that
  fit, at least 2. It has a mark every millimetre, a taller one every 5 and a numbered one
  every 10. Its label says which length it is.
- **FR-005**: The longer and shorter buttons change the density by 1 % (under a
  millimetre on a bank card's length), between 1 and 100, and keep it at once. Before the first measurement the ruler
  starts at 96 pixels per inch (3.78 per millimetre) and nothing is kept until the person
  changes it.
- **FR-006**: The number field takes digits and one decimal separator (dot or comma);
  confirming keeps a number from 1 to 100; any other is refused with a note, and nothing
  is kept.
- **FR-007**: The ruler is only true on the display Settings is on. On another display's
  page the ruler is not drawn; a note says to move Settings to that display to measure it,
  and the number field stays.
- **FR-008**: Device-neutral: no device names; the file is per person, like the gestures
  file.

## Contracts

| Interface | Ownership | Version | Where |
|---|---|---|---|
| `~/.config/glassine/output-density` (`DENSITY OUTPUT-KEY` lines) | owned (Settings writes); consumed by the compositor, which gives the pen interface the density over the output's scale for an output of unknown size, and by the handwriting service through it | 1 | this spec; README "Runtime interfaces" |
| The compositor's outputs (name, make, model, serial, scale, position) | consumed (`swaymsg -t get_outputs`) | — | GlassineOS/sway |
| An output's physical size | consumed (the display server's output geometry) | — | the Wayland core protocol |
| Output density setting | owned here, required by handwriting spec 004 FR-017 | — | GlassineOS/handwriting `specs/004-handwriting-for-apps/spec.md` |

The compositor never reports a size derived from this file as the output's physical size;
if it did, FR-001 still lists the output, because it has a line.

## Device scope and risk

Device-neutral. It writes one file in the person's configuration.

- **Failure modes:** a wrong measurement makes handwriting misjudge the size of writing on
  that display; nothing else reads the file.
- **What it never touches:** partitions, the boot chain, the recovery, system files.
- **Escape route:** Forget on the display's page, or removing the line or the file.

## Acceptance

Host level (`make check`, `test/settings`): reading the file (none, comments, a key with
spaces, a bad number, out of range, two lines for one key), a write that keeps the other
lines, replaces the output's line and leaves no temporary file, Forget; the output key
from make, model and serial and the connector-name fallback; the list of outputs (size
known, unknown, known with a kept line); the ruler's length (the card when it fits, whole
centimetres when it does not) and its length in logical pixels at a scale; the number
field (comma, a second separator refused, out of range refused); renders of Appearance
with and without an output of unknown size and of the output's page (not measured,
measured, on another display, a refused number) in every theme and window size.

Hardware: on a display of unknown size, match the ruler to a bank card; write with the pen
on that display and see the handwriting service get the density over the output's scale.

## Out of scope

- Changing the density of a display that reports its size.
- Measuring in another way (a diagonal in inches, a known model list).
