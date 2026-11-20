# Feature Specification: The picture of the screen

**Feature Branch**: `screen-picture`
**Status**: Draft
**Input**: "Screenshot on hibernation: before hibernating, the screen picture is saved as
the one-time boot splash override, so the next start shows it." Boot and the loader take a
picture of the screen as it is; they know nothing of what it shows. The preview is wanted
on ordinary displays as much as on e-ink.
**Builds on**: GlassineOS/boot [011 boot splash](https://github.com/GlassineOS/boot/blob/main/specs/011-boot-splash/spec.md)
(the one-time override, "The hibernate picture"), this repository's 001 (the session and
its compositor) and 007 (hibernate after sleep).

## Context

A machine that resumes from hibernation shows nothing of the session until the resume is
done. The boot splash store can show one picture at the next start, long before that
(011). What it needs from the session is a picture of the screen: what the person saw
when the machine went down, as it stands on the display, at the display's own size. One
command takes it from the running compositor. It is the same on every machine and every
display; it knows nothing of the apps, the shell's views or the panel.

## Terms

| Term | Meaning |
|---|---|
| the screen | the session's first display, whole, as the compositor composes it (every window, panel and popup on it) |
| the picture | the screen as the person sees it: turned by the display's current rotation so it stands upright, in the display's own pixels |
| grey display | a display whose compositor output holds grey only (an e-ink panel's) |

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

## Contracts

| Contract | Owned / consumed | Spec location |
|---|---|---|
| `glassine-screen-picture OUT` and its exit status | owned; used by GlassineOS/boot's hibernate step | README, "Runtime interfaces" |
| the compositor's copy of its output (wlr-screencopy) | consumed (sway-glassine, sway) | — |

## Edge cases

- **Several displays:** the first one only.
- **A rotated session (landscape on a portrait panel):** the picture stands as shown; a
  start whose earliest stage draws in another orientation doesn't show it (011 FR-008).
- **A display whose mode the earliest stage doesn't share (a PC's firmware mode):** the
  same; the picture is taken at the session's mode all the same.
- **A grey display whose copy holds its grey in one colour channel:** grey, from that
  channel.

## Success criteria

- **SC-001** In the test session, the picture of a known screen matches it pixel for
  pixel, in a portrait and a landscape orientation.
- **SC-002** Without a session, the command exits 3 within its time limit and writes no
  file.
