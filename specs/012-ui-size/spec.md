# Feature Specification: The size of the interface

**Feature Branch**: `ui-size`
**Status**: Draft
**Input**: "The interface has a size in millimetres, not in pixels: a button is as large on one
display as on another, and the person can switch to a smaller (or larger) interface in
Settings, built on the density of spec 011."
**Builds on**: [011 the size of a display of unknown size](../011-output-density/spec.md)
(an output's density); GlassineOS/ui [002 Interleaf](https://github.com/GlassineOS/ui/blob/main/specs/002-interleaf/spec.md)
(the tokens and their pixels); GlassineOS/handwriting [004 handwriting for apps](https://github.com/GlassineOS/handwriting/blob/main/specs/004-handwriting-for-apps/spec.md)
FR-017 (the pen interface's points per millimetre).

## Context

Every size in the desktop's design (Interleaf's tokens, the shell's bar, Settings' rows, Home's
grid, the keyboard) is given in design pixels. How large a design pixel is on the glass depends on
the display: the same number of pixels is a large button on a coarse monitor and a small one on a
fine panel. This spec gives the design pixel a length. Each display's density (spec 011) turns
that length into the display's own pixels, so a control has the same physical size on every
display, and the person picks how large the whole interface is with one setting.

## Terms

| Term | Meaning |
|---|---|
| output, output key, density, size unknown | as in spec 011 |
| design pixel | the unit of every size in the design: Interleaf's tokens, the boards in GlassineOS/ui `designs/`, the drawn programs' layouts |
| interface size | the person's choice of how large the interface is: one of the steps of FR-010, a percentage |
| factor | an output's device pixels per design pixel: what the compositor gives the output as its scale, so a design pixel is a logical pixel |
| drawn programs | the programs that draw with the shared UI library (GlassineOS/ui): the shell's bar, panels and overlays, Home, Settings, the first-run setup, the sleep pill, and any app built on it |
| foreign apps | apps that draw with another toolkit (GTK, Qt, a browser engine) |

## User scenarios

### Scenario 1 – A smaller interface (Priority: P1)

**Given** the interface at Default,
**When** the person opens Appearance and picks Smaller under "Size and text" › "Interface size",
**Then** the bar, Home, Settings and every other drawn program redraw at once, smaller, in a single
full refresh of each display; their layouts show more (more rows in a list, more columns in a grid),
not the same layout shrunk with empty space around it; the choice is kept, and the next session
starts with it.

### Scenario 2 – The same size on two displays (Priority: P1)

**Given** a tablet's panel and a monitor of another density in one session,
**When** the person looks at Settings on one and then moves it to the other,
**Then** its rows, text and buttons measure the same millimetres on both (within a whole device
pixel), and the window redraws for the display it is on when it moves.

### Scenario 3 – Text and targets stay usable (Priority: P1)

**Given** the smallest interface size on the finest display,
**When** the person reads a page of Settings and taps through it with a finger,
**Then** running text is at least 2.8 mm high (em size) and no text is under 2.0 mm, and every
control answers a fingertip: none is smaller than 7 mm in either direction.

### Scenario 4 – A display of unknown size (Priority: P2)

**Given** a monitor that reports no size and has not been measured,
**When** it is connected,
**Then** the interface on it is drawn at one device pixel per design pixel times the interface size,
and its Appearance row says "Not measured"; measuring it (spec 011) redraws it at its true size at
once.

### Scenario 5 – Other apps follow (Priority: P2)

**Given** the person picks Large,
**When** they look at a GTK app, a Qt app and a browser,
**Then** each is larger in step with the drawn programs as far as its toolkit allows (FR-030 to
FR-033); Settings says which apps follow only when they next start.

## Requirements

**The unit**

- **FR-001**: At the interface size Default, a design pixel is 0.224 mm on every display. At another
  interface size it is 0.224 mm times the size's percentage.
- **FR-002**: An output's factor is its density (FR-005) times the design pixel's length in
  millimetres (FR-001). A factor within 3 % of a whole number is that whole number, so a display
  whose density is close to a whole multiple of the unit is drawn without fractional scaling.
- **FR-003**: The factor is applied per output and is the output's scale, given to the apps on it as
  FR-030 and FR-031 say. Two outputs of different densities get different factors at the same
  interface size.
- **FR-004**: The pen interface's points per millimetre stay the density over the output's scale
  (spec 011, handwriting spec 004 FR-017), whatever the factor.

**The density of an output**

- **FR-005**: An output's density comes from, in this order: the physical size the output reports
  (FR-006), the density kept for it (spec 011), the fallback (FR-007).
- **FR-006**: A reported size counts only when it is plausible: a width and height that both give a
  density from 1 to 100 pixels per millimetre and are not the output's aspect ratio in centimetres
  (such as 16 × 9 or 16 × 10 cm). An output whose reported size does not count is "size unknown" for
  spec 011, so Settings lists it for measuring.
- **FR-007**: The fallback density, for an output with neither a plausible reported size nor a kept
  density, is 4.464 pixels per millimetre (1 / 0.224): such an output is drawn at one device pixel
  per design pixel at Default. Handwriting keeps its own documented default for such an output
  (handwriting spec 004 FR-017); the two are not tied.

**The setting**

- **FR-010**: Appearance's "Size and text" section has an "Interface size" control with four
  steps: Smaller (80 %), Small (90 %), Default (100 %) and Large (120 %). Under it, the current
  step's sizes in millimetres: the voice's row text and a standard button, as drawn on the display
  Settings is on.
- **FR-011**: A step applies at once to every output: the session sets each output's new factor, the
  drawn programs lay out and draw again at it, and each display gets one full refresh (as a theme
  change does). Nothing asks to confirm.
- **FR-012**: The interface size is kept per person in `$XDG_CONFIG_HOME/glassine/ui-size` (default
  `~/.config/glassine/ui-size`): one line, the step's percentage as a whole number (`80`, `90`,
  `100`, `120`). A missing file, any other content or any other number counts as 100. A write
  replaces the file as a whole (written beside it, then renamed).
- **FR-013**: One interface size holds for every output; how large an output's pixels are is
  covered by its density, not by a size per output.
- **FR-014**: Appearance names, beside "Interface size", the density of the display Settings is on
  and where it came from ("reported by the display", "measured", "not measured"); for a display that
  can be measured, it links to its Screen size page (spec 011).

**Drawn programs**

- **FR-020**: Drawn programs lay out for the logical size of their output or window at its factor.
  No layout assumes a fixed screen size in design pixels; a smaller interface size gives a larger
  logical size, and every page, grid and panel fills it (more rows, columns or items), with the fit
  rules each program already has for small windows applying to large interface sizes too.
- **FR-021**: Drawn programs draw at the device resolution of their output: text, icons and shapes
  are drawn at the factor, never drawn at another scale and resampled. Lines and edges land on whole
  device pixels and are never thinner than one device pixel.
- **FR-022**: Minimum sizes, in millimetres on the glass, whatever the interface size and display:
  running text and row labels 2.8 mm (em size); any other text 2.0 mm; any control that takes a tap
  7 mm in width and in height, including its padding. An element whose design size would fall below
  its minimum is drawn at the minimum; the steps of FR-010 are chosen so that only the smallest type
  styles reach it.
- **FR-023**: The on-screen keyboard's keys are sized in the same unit: its own size choice (Small,
  Medium, Large) is relative to the interface size, and a key is never under 7 mm.
- **FR-024**: Text size (where Appearance offers it) scales text only, on top of the interface size,
  and is held to the same minimums.
- **FR-025**: Everything the shell draws over the screen follows the factor of the output it is on:
  the bar, Overview, quick settings, the power view, notifications, the shutdown screen. A program
  that draws for a display outside the session (the sleep pill, the start picture) draws at the
  factor the session last applied to that output (FR-026); when no session has applied one, at the
  Default factor of the output's density (the fallback of FR-007 when it has none).
- **FR-026**: The session publishes, for each output, the factor it applied, the density, and where
  the density came from, in `$XDG_RUNTIME_DIR/glassine/output-scale`: one line per output,
  `FACTOR DENSITY SOURCE OUTPUT-KEY`, the two numbers decimal with a dot, SOURCE one of `reported`,
  `measured`, `fallback`, then the output key (spec 011) to the end of the line. It is replaced as a
  whole whenever a factor changes.

**Foreign apps**

- **FR-030**: Apps that take a fractional scale from the display server draw at the output's exact
  factor and so follow the interface size exactly.
- **FR-031**: Apps that take only whole-number scales are given the whole number nearest the
  factor and are shown pixel for pixel, never resampled; the remainder (the factor over that whole
  number) reaches their text through the desktop's text settings, so their text follows the
  interface size and their other sizes follow it to the nearest whole step.
- **FR-032**: The pointer's size and the desktop's text settings (the interface font sizes the
  themes set) follow the interface size, so toolkits that read them follow too, live where the
  toolkit re-reads them.
- **FR-033**: An app that reads none of these keeps its own size. When the interface size changes,
  Settings says once that apps already open may follow only when they next start.

**Device scope**

- **FR-040**: Device-neutral: no device names; the unit is the same on every device, and a device
  differs only in its density.

## Contracts

| Interface | Ownership | Version | Where |
|---|---|---|---|
| `~/.config/glassine/ui-size` (the interface size, a percentage) | owned (Settings writes; the session reads) | 1 | this spec FR-012; README "Runtime interfaces" |
| `$XDG_RUNTIME_DIR/glassine/output-scale` (`FACTOR DENSITY SOURCE OUTPUT-KEY` lines) | owned (the session writes); consumed by the drawn programs outside the session (the sleep pill, the start picture) | 1 | this spec FR-026; README "Runtime interfaces" |
| `~/.config/glassine/output-density` | owned by spec 011; consumed here for an output's density (FR-005) | 1 | spec 011 FR-002 |
| An output's physical size and scale; fractional scale | consumed (the display server's output geometry; the fractional-scale protocol) | — | the Wayland core protocol; wayland-protocols |
| The compositor's outputs and their scale | consumed (`swaymsg -t get_outputs`, `output … scale`) | — | GlassineOS/sway |
| The design pixel as the unit of Interleaf's tokens and of the boards | consumed | Interleaf tokens 1 | GlassineOS/ui spec 002 |

## Device scope and risk

Device-neutral. It writes one file in the person's configuration and one runtime file of the
session, and sets the compositor's output scales.

- **Failure modes:** a wrong density (a display that reports a wrong size, a wrong measurement) draws
  the interface at the wrong physical size on that display; the minimums of FR-022 are then off by
  the same ratio. A missing or unreadable `ui-size` file gives Default.
- **What it never touches:** partitions, the boot chain, the recovery, system files, the loader's
  and the recovery's own drawing.
- **Escape route:** pick Default again in Appearance, or remove `~/.config/glassine/ui-size`; for a
  wrong measurement, Forget on the display's Screen size page (spec 011).

## Acceptance

Host level (`make check`):

- the factor: an output's density from a reported size, a kept density, the fallback; a reported
  size refused as implausible (out of range, an aspect ratio in centimetres); each step at a density
  of 8.94, 3.6 and 13 pixels per millimetre and with none; the snap to a whole number within 3 % and
  not beyond;
- the setting file: none, each step, a bad line, an unknown number; a write that leaves no temporary
  file; the runtime file after a change, for two outputs of different densities;
- the minimums: every Interleaf type style, every voice's row and secondary text, and every
  control's height in the drawn programs, at each step and at densities from 3 to 25 pixels per
  millimetre, measured in millimetres against FR-022, and the elements that reach a minimum listed;
- renders of the bar, Overview, Home and every Settings page at each step on a portrait and a
  landscape output in every voice, with the layout filling the logical size (FR-020) and no line
  thinner than a device pixel (FR-021);
- under headless sway with two outputs of different scales: a drawn program moved between them
  redraws at the new factor; a whole-scale-only GTK app gets the nearest whole scale and the text
  setting of FR-031.

Hardware: on a tablet's panel and an external monitor at once, measure a button and a row's text with
a ruler on both at each step; switch steps and see one full refresh per display; write with the pen
on both and see the handwriting service get the density over the scale.

## Out of scope

- An interface size per display.
- Changing the density of a display that reports a plausible size.
- The loader's start menu and the recovery, which draw before any session and have no person's
  settings.
- Sizes of foreign apps beyond what their toolkit takes from the display server and the desktop's
  text settings.
