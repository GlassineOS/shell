# Feature Specification: The picture of the screen

**Feature Branch**: `screen-picture`
**Status**: Draft
**Input**: "Screenshot on hibernation: before hibernating, the screen picture is saved as
the one-time boot splash override, so the next start shows it." Boot and the loader show
the picture they are handed; they know nothing of what it shows. What it holds is the
shell's choice: the screen, with a notice over it that the session is on its way back,
in the orientation the next start draws in. The preview is wanted on ordinary displays as
much as on e-ink.
**Builds on**: GlassineOS/boot [011 boot splash](https://github.com/GlassineOS/boot/blob/main/specs/011-boot-splash/spec.md)
(the one-time override, "The hibernate picture"), this repository's 001 (the session and
its compositor), 004 (the sleep pill, whose resume mode is the notice), 005 (autorotate:
the session's turn) and 007 (hibernate after sleep).

## Context

A machine that resumes from hibernation shows nothing of the session until the resume is
done. The boot splash store can show one picture at the next start, long before that
(011). What it needs from the session is a picture of the screen: what the person saw
when the machine went down, as it stands on the display, at the display's own size. One
command takes it from the running compositor. It is the same on every machine and every
display; it knows nothing of the apps, the shell's views or the panel.

A bare picture of the session at the next start looks like a frozen desktop. So the
picture the next start shows, the start picture, is the shell's own: the screen with the
resume notice over it, a pill like the one the machine went down with (the sleep pill's
resume mode, 004), in the same voice, size and place, so that from the pill on screen
before the machine went off to the start picture only the words change. The earliest
stage of a start draws in the display's resting orientation, before any session turns
it; the start picture is turned back into it, so the display shows it the way the
session did.

## Terms

| Term | Meaning |
|---|---|
| the screen | the session's first display, whole, as the compositor composes it (every window, panel and popup on it) |
| the picture | the screen as the person sees it: turned by the display's current rotation so it stands upright, in the display's own pixels |
| grey display | a display whose compositor output holds grey only (an e-ink panel's) |
| the session's turn | how far the session has turned its content clockwise from the display's resting orientation (005's applied turn); none: 0 |
| the resting frame | the picture turned back by the session's turn: the display as it stands with no turn, the frame the panel daemon and the sleep pill draw in |
| the resume notice | the sleep pill (004) in its resume mode: the device's on its way back |
| the start picture | the picture in the resting frame with the resume notice over it: what the hibernate step hands to the next start |

## User scenarios

### Scenario 1 – A picture for the hibernate step (Priority: P1)

**Given** a running session,
**When** the system asks for the picture of the screen,
**Then** within a few seconds a picture file holds the screen as it was at that moment,
upright, at the display's size: grey on a grey display, colour on any other.

### Scenario 2 – Nothing to take (Priority: P1)

**Given** no running session, a compositor that offers no copy of its screen, or a copy
that fails,
**When** the system asks for the picture,
**Then** the command says so with its own exit status, within its time limit, and writes
nothing.

### Scenario 3 – The start picture says the session is coming back (Priority: P1)

**Given** a running session, in any voice and any of the four turns,
**When** the hibernate step asks for the start picture,
**Then** within a few seconds a picture file holds the screen in the display's resting
frame, with the resume notice where the sleep pill sits (centred at the bottom edge of
what the person saw, upright for them), drawn the way that pill is, and the rest of the
screen unchanged pixel for pixel.

### Scenario 4 – A landscape session on a portrait display (Priority: P1)

**Given** a session turned to landscape on a display whose resting orientation is
portrait,
**When** the hibernate step asks for the start picture,
**Then** the picture is portrait, the display's resting frame, and the display shows it
as the session was shown: the content sideways in the same sense, the notice at the
person's bottom edge. With no turn (a PC), the picture is the screen as it is.

### Scenario 5 – No notice, no start picture (Priority: P2)

**Given** a session whose notice cannot be drawn (no renderer, no usable fonts, its time
limit passed),
**When** the hibernate step asks for the start picture,
**Then** the command writes nothing and says "nothing to take": the next start shows its
ordinary splash, never the bare screen.

## Requirements

- **FR-001** One command, `glassine-screen-picture OUT`, writes the picture to OUT. It
  runs as any user who can reach the session's compositor, the system's own steps
  included: it uses the session named by its environment, else the first session it
  finds in the runtime directories.
- **FR-002** The picture is the whole first display as the compositor last composed it,
  turned by the display's current rotation so that it stands as the person sees it, in
  the display's own pixels (not scaled).
- **FR-003** The file is a binary portable anymap: grey (8 bits a pixel) for a grey
  display, RGB (8 bits a channel) for any other. A display counts as grey when the copy
  holds one channel only.
- **FR-004** It is written under a temporary name and then renamed, so OUT is the whole
  picture or isn't there.
- **FR-005** The command ends within a few seconds whatever happens. Its exit status
  says: written (0), nothing to take (3: no session, no copy offered, no display, a copy
  that failed, a pixel layout it doesn't know, its time limit passed), usage (2), OUT not
  written (1).
- **FR-006** It knows nothing of the screen's content: no app, view or lock is treated
  apart. It takes no input and changes nothing on screen.

### The start picture

- **FR-007** One command, `glassine-start-picture OUT`, writes the start picture to OUT.
  It is what the hibernate step asks for; the step writes it as it is. It runs as any
  user who can reach the session (FR-001).
- **FR-008** The start picture is the picture of the screen (FR-002) turned back by the
  session's turn into the resting frame. Without a turn (no autorotate, a PC) it is the
  picture as it is. The turn is the shell's own record of what it applied (005), never a
  device's name or a size it knows of.
- **FR-009** Over it lies the resume notice: the sleep pill's resume mode, drawn by the
  same renderer, in the same voice, at the same size and place as the sleep pill for that
  screen and turn (004 FR-002 to FR-005), so that only the words differ from the pill the
  machine went down with. Its words are the renderer's (004 FR-001a), written in one place.
- **FR-010** Outside the notice, the start picture is the screen pixel for pixel. It is
  grey (P5) when the picture of the screen is, else RGB (P6), at the resting frame's size,
  written under a temporary name and renamed (FR-004).
- **FR-011** Without the notice there is no start picture: when the notice can't be
  drawn, the command writes nothing and exits 3, as when there is nothing to take. Its
  exit status is FR-005's.
- **FR-012** The command ends within a few seconds whatever happens: the picture of the
  screen within its own limit (FR-005), the notice within its own.

## Contracts

| Contract | Owned / consumed | Spec location |
|---|---|---|
| `glassine-screen-picture OUT` and its exit status | owned | README, "Runtime interfaces" |
| `glassine-start-picture OUT` and its exit status | owned; used by GlassineOS/boot's hibernate step (011 FR-024) | README, "Runtime interfaces" |
| the session's turn (`/run/glassine-rotation`) | owned (005) | README, "Runtime interfaces" |
| `glassine-sleep-pill -m resume -b` (the notice over a picture) | owned (004) | README, "Runtime interfaces" |
| the compositor's copy of its output (wlr-screencopy) | consumed (sway-glassine, sway) | — |

## Edge cases

- **Several displays:** the first one only.
- **A rotated session (landscape on a portrait panel):** the picture of the screen stands
  as shown; the start picture is in the resting frame (FR-008), the orientation the
  earliest stage draws in.
- **A display turned by the compositor's own configuration, not by the session's turn (a
  PC monitor on its side):** no session turn, so the start picture stands as shown; an
  earliest stage that draws in another mode doesn't show it (011 FR-008).
- **A display whose mode the earliest stage doesn't share (a PC's firmware mode):** the
  same; the picture is taken at the session's mode all the same.
- **The notice over an app's own text at the bottom edge:** it covers it, as the sleep
  pill does.
- **A grey display whose copy holds its grey in one colour channel:** grey, from that
  channel.

## Success criteria

- **SC-001** In the test session, the picture of a known screen matches it pixel for
  pixel, in a portrait and a landscape orientation.
- **SC-002** Without a session, the command exits 3 within its time limit and writes no
  file.
- **SC-003** In the test session, the start picture of a known screen is that screen
  turned back by the turn, exact outside the notice, with the notice centred at the
  bottom of the person's view, for no turn and a turned output; the notice's box equals
  the hibernate pill's for every voice and turn.
- **SC-004** Without a session or without a notice, `glassine-start-picture` exits 3 and
  writes no file.
