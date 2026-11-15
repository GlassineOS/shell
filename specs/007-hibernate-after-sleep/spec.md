# Feature Specification: Hibernate after sleeping

**Feature Branch**: `settings-autohibernate`
**Status**: Draft
**Input**: "Add an auto-hibernate setting to the settings app: how long the device sleeps
on battery before it hibernates."

## Context

The power daemon reads `hibernate_after_min` from `/etc/glassine/power.conf`. When the
device has slept on battery that long, an RTC alarm wakes it and it hibernates: the
session is saved to the hibernation swap file and the device turns off; the power key
resumes it. A missing key means 4320 (three days); 0 or less means never. The daemon
re-reads the file when it changes (within 10 s while awake) and takes the value at the
start of each sleep.

Settings > Power & sleep shows this as one row in its "Timers" section, beside "Sleep
after" and "Wi-Fi off after".

## User scenarios

### Scenario 1 – Choose how long the device sleeps before it hibernates (Priority: P1)

**Given** a device with hibernation set up,
**When** the person opens Power & sleep,
**Then** the Timers section has a "Hibernate after" row ("While asleep on battery")
with the choices 1 h, 6 h, 1 day, 3 days, 1 week and Never, showing the current value,
and below it one line: "When the device has slept on battery this long, it saves your
session and turns off. The power key resumes it."

**Given** that row,
**When** the person steps to another choice,
**Then** `power.conf` has the new `hibernate_after_min` at once, every other line of the
file is kept, nothing is restarted, and the next sleep uses it.

### Scenario 2 – A value set by hand (Priority: P2)

**Given** `hibernate_after_min` set by hand to a value that is not one of the choices,
**When** the person opens Power & sleep,
**Then** the row shows that value as its own step, in order among the choices (e.g.
"90 min", "2 days"), and stepping away from it writes a regular choice.

### Scenario 3 – No hibernation on this device (Priority: P1)

**Given** a device where hibernation is not set up,
**When** the person opens Power & sleep,
**Then** there is no Hibernate row and no explanation line; the rest of the page is
unchanged.

## Requirements

- **FR-001**: Hibernation counts as set up exactly as for the shell's Power popup
  (`pwr_hib_ok()` in `src/glassine-shell.c`): `/usr/sbin/glassine-hibernate` is
  executable, `/etc/glassine/deploy.conf` does not set `HIBERNATE_SIZE_MB` to 0, and the
  swap file `USERDATA_MNT/USERDATA_PREFIX/swap/hibernate` (defaults
  `/run/glassine/userdata` and `media/glassine`; one pair of quotes around a value
  dropped) is a regular file. A stat refused with "permission denied" counts as present
  (the directory is root's). Checked each time the page opens.
- **FR-002**: The shown value is read as the power daemon reads it: a missing key or an
  empty value is 4320; a value whose leading digits give 0 or less, or that has no
  leading digits, is Never (0).
- **FR-003**: The choices and the minutes written: 1 h = 60, 6 h = 360, 1 day = 1440,
  3 days = 4320, 1 week = 10080, Never = 0. Never is the last step, as in "Wi-Fi off
  after".
- **FR-004**: Any other positive value is one more step at its place by minutes, labelled
  in whole days ("2 days"), else whole hours ("12 h"), else minutes ("90 min").
- **FR-005**: The write goes to `/etc/glassine/power.conf` as the desktop user, in place
  and under the file's lock, like the other timers (the file is group-writable for the
  session); no administrator password.
- **FR-006**: Device-neutral: no device names; the paths are the ones every image uses.

## Contracts

| Interface | Ownership | Version | Where |
|-----------|-----------|---------|-------|
| `hibernate_after_min` in `/etc/glassine/power.conf` | consumed (`GlassineOS/packages`, the device's power daemon); written by Settings | — | the power daemon's header |
| `/usr/sbin/glassine-hibernate`, `HIBERNATE_SIZE_MB`, `USERDATA_MNT`, `USERDATA_PREFIX` in `/etc/glassine/deploy.conf`, the swap file `swap/hibernate` | consumed (`GlassineOS/boot`) | — | `docs/deployments.md` |

## Device scope and risk

Device-neutral. It writes one key of `power.conf`, as the other timers do.

- **Failure modes:** a short value hibernates sooner than the person expects (the power
  key resumes the session); the power daemon notices a file change by its modification
  time in whole seconds, so two writes within one second can leave it on the first until
  the next write.
- **What it never touches:** partitions, the boot chain, the recovery, the swap file,
  user data.
- **Escape route:** choose Never, or remove the key from `power.conf` for the default.

## Acceptance

Host level (`make check`, `test/settings`): the reading of the key (missing, empty, each
choice, 0, negative, not a number, a custom value), the steps with a custom value and
their labels, a write of each choice that keeps the file's other lines, and the
set-up check (no helper, `HIBERNATE_SIZE_MB=0` with and without quotes, no swap file, a
directory instead of a file, a swap file, permission denied, other mount and prefix);
renders of the page in every theme and window size with the row, with a custom value and
without hibernation.

Hardware: on a device with hibernation, pick 1 h, sleep on battery, and see it hibernate
after an hour; the power key resumes the session.

## Out of scope

- Hibernation while charging (the kernel refuses to suspend with USB connected).
- Setting up hibernation (the swap file and its size) from Settings.
