# Feature Specification: Autorotate

**Feature Branch**: `autorotate`
**Created**: 2026-11-12
**Status**: Draft
**Input**: "Bring the screen autorotation over: a daemon that turns the panel output with
the device, as the quick settings and the settings app already expect it."

## Context

The quick settings and the settings app already offer a Rotation row (auto, or one of
four fixed turns) and write the choice for a rotation daemon; the bar and the window
model already read the turn it applies. The daemon itself is not in this repository yet.

The behaviour comes over unchanged from the earlier private tree, except that what
differs per device (which accelerometer, which output, how the sensor sits in the
case) comes from configuration.

## User scenarios

### Scenario 1 – The screen follows the device (Priority: P1)

**Given** rotation set to auto, **When** the person holds the device in another of the
four orientations for half a second, **Then** the desktop turns to it; the e-ink panel
keeps the old picture until the turned desktop has redrawn and then shows it in one
full refresh.

### Scenario 2 – Lying flat or tilted near 45 degrees changes nothing (Priority: P1)

**Given** rotation set to auto, **When** the device lies flat or is held near the
diagonal between two orientations, **Then** the screen keeps its current turn.

### Scenario 3 – A fixed turn (Priority: P1)

**Given** the person picks one of the four turns in the quick settings or settings,
**When** the choice is saved, **Then** the screen turns to it within about a second and
the sensor is no longer read.

### Scenario 4 – Sleep doesn't break it (Priority: P1)

**Given** the power daemon pauses the sensor stream for a suspend and resumes it after,
**When** the device wakes, **Then** autorotation works again without a restart.

### Scenario 5 – A new desktop session keeps the turn (Priority: P2)

**Given** a turned screen, **When** the compositor restarts, **Then** the new session
gets the same turn again.

## Requirements

- **FR-001**: The rotation setting is one line: `auto` (also when the file is missing or
  unreadable), `lock N` (N = 0, 90, 180, 270: that turn, no sensor) or `lock` (no sensor,
  the turn stays as it is). Changes take effect within about a second, without a restart.
- **FR-002**: In auto mode the daemon reads the accelerometer's screen-plane gravity at a
  low configurable rate (default 4 Hz). Below half of g in the screen plane the device is
  flat and the turn stays. Otherwise one axis must exceed the other by 1.5 times (about
  56 degrees from the other axis), and the new orientation must hold for a configurable
  time (default 500 ms) before the screen turns.
- **FR-003**: A turn is the output transform (clockwise degrees, as the compositor's
  `transform`), sent over the compositor's IPC. Right before it the e-ink daemon gets
  `rotate` on its control FIFO when that FIFO exists. The applied turn is published as one
  number for the shell and the settings; it survives a restart of the daemon.
- **FR-004**: In lock mode, and when the daemon exits, the sensor stream is off and the
  sampling rate is what it was before.
- **FR-005**: A paused sensor stream (its buffer disabled by another process) is waited
  out; the daemon never turns a stream on again on its own while it holds it open. A
  stream that disappears (device gone, driver reloaded) is found and opened again.
- **FR-006**: A new compositor IPC socket gets the current turn again. Without a
  compositor, a turn is retried with the next stable orientation.
- **FR-007**: No device names in the code. Per device: the accelerometer's IIO name, the
  output name and which signed sensor axes point to the screen's right and bottom edges,
  from a device configuration file. Without a matching accelerometer the daemon still
  applies fixed turns and looks for the sensor again every few seconds.
- **FR-008**: A replay of a recorded sample stream prints the decisions (for tuning, and
  the host tests); a dry run logs without turning anything.

## Contracts

| Contract | Owned / consumed | Version | Spec location |
|---|---|---|---|
| `/etc/glassine/rotation`: `auto` \| `lock` \| `lock N` | owned (shell; the root helper writes it) | 1 | FR-001; README "Runtime interfaces" |
| `/run/glassine-rotation`: the applied turn, one number | owned (shell; the bar and the window model read it) | 1 | FR-003; README |
| `/etc/glassine/rotd.conf`: `KEY=value` (`SENSOR`, `OUTPUT`, `AXES`, `RATE_HZ`, `HOLD_MS`) | owned (shell); a device's image stage ships it | 1 | `src/glassine-rotd.c` header, README |
| OpenRC service `glassine-rotd` | owned (shell); the image adds it to a runlevel | 1 | README |
| `rotate` on `/run/glassine-epdd-desktop.ctl` | consumed (`GlassineOS/epd`) | — | — |
| `output NAME transform N` on the sway IPC socket | consumed (sway as the images ship it) | sway 1.10 | — |
| IIO buffered accelerometer (`scan_elements`, `buffer/enable`, sampling frequency) | consumed (the device kernel) | — | — |

## Device scope and risk

Device-neutral; a device without an accelerometer gets fixed turns only. It writes only
the sensor's IIO attributes and its own state file under `/run`.

- **Failure modes:** a wrong axis mapping turns the screen the wrong way (fix: the device
  configuration, or a fixed turn from the quick settings); a stream left on keeps waking
  the system (the daemon turns it off in lock mode and on exit, and the power daemon pauses
  it for a suspend).
- **What it never touches:** partitions, the boot chain, the recovery, user data.
- **Escape route:** a fixed turn in the quick settings or settings, or stopping the
  service (`rc-service glassine-rotd stop`); the turn then stays until the session restarts.

## Acceptance

Host level (`make check`): the decision logic by replay (four orientations, flat and
diagonal samples kept, the hold time), and the daemon against a stand-in IIO device and
headless stock sway: auto turns, a paused stream, fixed turns with the stream off and the
rate restored, back to auto, a missing sensor with a fixed turn.

Hardware: supervised, on the first device the e-ink stage ships to: the four
orientations by hand, a suspend and wake, a fixed turn.

## Out of scope

- Per-app orientation requests and rotation of input devices beyond what the compositor
  maps to the output.
- A settings UI for the device configuration.
