# Feature Specification: Restart and Restart fully

**Feature Branch**: `soft-restart`
**Status**: Draft
**Input**: GlassineOS/boot spec 013 (soft restart), FR-007 and FR-010: the power menu's
two restart entries and what "Restart" says before the person taps it.

## Context

GlassineOS/boot's restart step (`glassine-deploy restart`) restarts softly where its rules
allow: the apps close and the next session starts without the hardware starting again.
Otherwise it restarts cold, as an ordinary restart. `restart --check --porcelain` says
which, with a reason word (boot's `docs/deployments.md`, "Restart"). `restart --cold`
always restarts cold. Both run the user-shutdown mark themselves, so a staged deployment
is switched as at any restart the person starts.

The Power popup is its own confirmation: one tap acts.

## User scenarios

### Scenario 1 – Restart (Priority: P1)

**Given** the Power popup,
**When** it opens,
**Then** the "Restart" row's second line says what a tap does: with a soft restart, the
apps it closes and that the device stays on; with a cold one, "An ordinary restart" and
the reason in plain words; where the system has no restart step (the plain layout, an
older tool), the apps it closes, as before.

**Given** that row,
**When** the person taps it,
**Then** the system runs the restart step (soft where it can be, else cold). If the step
refuses before anything stops (busy, too little space for the switch), the device
restarts the ordinary way. Without a restart step it restarts the ordinary way.

### Scenario 2 – Restart fully (Priority: P1)

**Given** the Power popup,
**When** the person taps "Restart fully" (the row after "Restart", always there),
**Then** the device restarts cold, through the hardware, whatever the step's rules say.

## Requirements

- **FR-001** The Power popup has a "Restart fully" row right after "Restart".
- **FR-002** As the popup opens, the shell asks the root helper (`glassine-power-root
  restart-check`) what Restart would be, without waiting for it; the row's line follows
  the answer once it arrives.
- **FR-003** The root helper's `reboot` runs `glassine-deploy restart`, `reboot-full` runs
  `glassine-deploy restart --cold`, each falling back to the user-shutdown mark and
  `reboot` where the step is missing or refused; it never reboots while the step's
  restart runs (`/run/glassine/restarting`).
- **FR-004** Every reason word of boot 013 has a line in the shell's words; an unknown
  word shows "An ordinary restart" alone.

## Not here

- The next session's one-time notice of why a soft restart turned cold (boot 013 FR-007,
  `cold_reason` in `/run/glassine/restart`), the restart picture (FR-031) and "Restart to
  update" in the update notice (FR-011).

## Success criteria

- **SC-001** In the shell's host test the Power popup has Restart and Restart fully, with
  the row counts of every hibernation and start-menu combination.
- **SC-002** The root helper's routing is tested with a stand-in restart step: soft,
  cold, refused, missing, older, and a restart that runs.
