# Feature Specification: Background apps and compressed memory

**Feature Branch**: `zram`
**Created**: 2026-11-06
**Status**: Draft
**Input**: "Place the background-app memory tooling in the shell (issue #3): the app
prioritizer that slows and freezes hidden apps and moves their memory out of RAM, the
per-app spill of that memory to disk, and the service that sets up compressed swap with
a spill area on the user data partition."

## Context

E-ink devices have little RAM and slow cores. People keep several apps open on other
workspaces; those apps keep using CPU, timers and memory while nobody looks at them,
which slows the app in front and drains the battery. The shell already starts the
background prioritizer with the session and asks it to thaw an app before closing a
hidden window, and the settings app already writes its per-app freeze rules, but the
programs themselves and the compressed-swap service are not in this repository yet.

The behaviour comes over unchanged from the earlier private tree, except that it now
only follows sway (the X session is gone, #2 / #8) and that device values come from
configuration. Images ship the compressed swap with its disk spill in the e-ink stage
(maintainer decision, 2026-11-06).

Verification status: host-tested in containers. The chain freeze → page-out →
compressed swap → disk spill has not run on a device with this code.

## User scenarios

### Scenario 1 – Hidden apps stop competing with the app in front (Priority: P1)

**Given** two apps on different workspaces, **When** the person works in one, **Then**
the other gets CPU time only when the front app doesn't want it, a few seconds after it
was hidden, and runs at full priority again the moment its workspace is shown.

### Scenario 2 – Long-hidden apps are frozen (Priority: P1)

**Given** an app hidden for longer than the freeze delay, **When** the next periodic
check runs, **Then** the app is frozen (no CPU, no timer wakeups) unless it plays audio,
is a media player, has a sandbox background permission, is part of the desktop, or is
listed as "never freeze". Showing it again thaws it before it repaints.

### Scenario 3 – Closing a frozen app works (Priority: P1)

**Given** a frozen app, **When** the shell closes its window, **Then** the shell asks
the prioritizer to thaw it first, and the app sees the close at once.

### Scenario 4 – Frozen apps leave RAM (Priority: P2)

**Given** compressed swap is on, **When** an app is frozen, **Then** its anonymous
memory moves into compressed swap right away instead of at the next app start; a thaw
brings pages back as the app touches them.

### Scenario 5 – Long-frozen apps move to disk, one app at a time (Priority: P2)

**Given** compressed swap with a disk spill, **When** an app has been frozen for the park
delay, **Then** exactly that app's swapped memory is written to the spill on the user
data partition (as raw pages, which the spill compresses); the app in front never causes
disk writes. A thaw during the move stops it. Without a spill, that memory is recompressed
in RAM with the second algorithm instead.

### Scenario 6 – No user data partition, no spill (Priority: P2)

**Given** a boot without the user data partition mounted (or not enough free space),
**When** the service starts, **Then** compressed swap comes up in RAM only and says why
there is no spill.

### Scenario 7 – Turning it off (Priority: P3)

**Given** a device where the spill causes trouble, **When** the maintainer stops the
service or sets the spill size to 0, **Then** swapped pages come back into RAM, the
spill is detached, and nothing else on the user data partition changes.

## Requirements

- **FR-001**: The prioritizer reads windows and workspaces from the running sway session
  (its IPC socket, waited for up to 60 s) and exits, releasing everything, when sway ends.
- **FR-002**: A process with a view on a visible workspace (or a sticky view) is in front;
  one whose views are all elsewhere (or in the scratchpad) is in the background. Processes
  without a view follow their nearest ancestor with one; sandboxed (Flatpak) processes
  follow a view owner with the same app id.
- **FR-003**: Background processes move to a low-weight CPU group with a utilization cap,
  idle I/O priority and 50 ms timer slack, after 3 s hidden; coming to the front undoes
  this at once.
- **FR-004**: After `freeze_after` seconds hidden (default 120; 0 = never) a background app
  is frozen as a whole tree, unless exempt (FR-005). `freeze NAME` freezes it as soon as it
  is demoted, without the exemptions; the desktop's own processes still win.
- **FR-005**: Never frozen: media players (an MPRIS name on the session bus), apps with a
  playing audio stream, Flatpaks with the background permission, the desktop itself
  (compositor, panel daemon, shell, session, audio server, message bus, the prioritizer)
  and names listed `nofreeze`. Process names compare as the kernel stores them (first 15
  characters).
- **FR-006**: The prioritizer thaws an app on a datagram `thaw CON_ID` (sway container id)
  on its socket, and thaws every frozen app when an output changes.
- **FR-007**: Frozen processes get the highest OOM score and their own value back on thaw.
- **FR-008**: After a pass that froze something, frozen processes' anonymous memory is
  paged out (`reclaim swap`: only while a swap device is on, the default; `always`; `off`).
- **FR-009**: `park_after` seconds after an app froze (default 600; 0 = never), that one
  app's swapped memory is moved to the spill through the zram service's `park` command
  (without a spill: recompressed in RAM), one app at a time; a thaw stops a running park. `writeback_after` (default 0 = off) runs
  one idle-age writeback instead.
- **FR-010**: Foreground boost: with `/etc/glassine/fg-boost` holding N (1-100), front
  apps get a utilization floor of N %; off in the power saver mode (`/etc/glassine/ddr`
  `saver`) and never for the desktop's own processes.
- **FR-011**: The background group's utilization cap is configurable (`bg_uclamp_max`,
  default 79 %: background work alone never asks for the top CPU frequency).
- **FR-012**: The prioritizer re-reads its configuration on every periodic pass (30 s).
- **FR-013**: The parker finds exactly the given processes' (or a cgroup's) zram slots,
  recompresses them with the second algorithm, writes them back to the backing device and
  flushes and drops the backing files' page cache; either step can be left out. The zram
  service's `park` writes back without recompressing when there is a spill (the writeback
  decompresses each slot) and only recompresses when there is none. It refuses when swapped pages sit on more
  than one swap device. A failed recompression is reported and the writeback still runs.
- **FR-014**: The zram service sets up one zram swap device (size, RAM limit, algorithm,
  optional recompression algorithm), priority 100, the configured swappiness, page-cluster 0.
- **FR-015**: With a spill size > 0 and the user data partition mounted, the service gives
  zram a backing device: a loop over a fresh sparse file on a btrfs mounted
  `compress-force=zstd:LEVEL`, which itself lives in a preallocated image file in the spill
  directory; without btrfs tools, a plain preallocated file. Without a mount, room or a
  backing-device-capable kernel: no spill, and zram runs in RAM only.
- **FR-016**: The spill never grows userdata's use past the configured reserve of free space.
- **FR-017**: `stop` turns the swap off (pages come back), resets zram, detaches the loops
  and unmounts the spill. When the swap can't be turned off (the pages don't fit in RAM)
  it fails and tears nothing down, and the service reports the failure. Commands: `start`, `stop`, `park (-c CGROUP | PID...)`,
  `writeback [SECONDS]`, `status`; anything else is a usage error (exit 2).
- **FR-018**: The spill directory defaults to `<USERDATA_MNT>/<USERDATA_PREFIX>/zram` from
  `/etc/glassine/deploy.conf` (defaults `/run/glassine/userdata`, `media/glassine`);
  `GLASSINE_DEPLOY_CONF` names another file, as for the deployments tool.
- **FR-019**: No device names or device constants in the code; per-device values come from
  `/etc/glassine/zram` and `/etc/glassine/bgprio.conf`.

## Contracts

| Contract | Owned / consumed | Version | Spec location |
|---|---|---|---|
| `/run/glassine-bgprio.sock`: datagram `thaw CON_ID` | owned (shell) | 1 | this spec FR-006; README "Runtime interfaces" |
| `/etc/glassine/bgprio.conf`: `NAME...`, `nofreeze NAME...`, `freeze NAME...`, `freeze_after`, `reclaim`, `writeback_after`, `park_after`, `bg_uclamp_max` | owned (shell; the settings app writes the freeze rules) | 1 | `src/glassine-bgprio.c` header, README |
| `/etc/glassine/fg-boost`: one number 0-100 | owned (shell; the session seeds it) | 1 | FR-010, README |
| `/etc/glassine/zram`: shell `KEY=value` (keys in README) | owned (shell); a device package or image stage may ship it | 1 | `rootfs/usr/sbin/glassine-zram` header, README |
| OpenRC service `glassine-zram` | owned (shell); the image adds it to a runlevel | 1 | README |
| `glassine-zram park|writeback|status` | owned (shell), used by the prioritizer | 1 | FR-017 |
| Spill directory `<prefix>/zram/` on userdata | owned (shell) | 1 | FR-018 |
| `/etc/glassine/deploy.conf` `USERDATA_MNT`, `USERDATA_PREFIX` | consumed (`GlassineOS/boot`, docs/deployments.md) | as documented there | — |
| `/etc/glassine/ddr` (power mode, first word) | consumed (the power-mode tool) | — | — |
| sway IPC (workspaces, tree, events) | consumed (sway as the images ship it) | sway 1.10 | — |
| Kernel: cgroup v2 with cpu controller and freezer, `process_madvise`, zram with writeback and multiple compression algorithms, recompression by page index | consumed (the device kernel) | — | — |

## Device scope and risk

Device-neutral. Sizes and paths come from configuration; a device without zram, cgroup
v2 or a user data partition gets less (no swap, no freezing, no spill) and logs why.

**It writes to the user data partition** (the spill: an image file and, inside it, the
writeback file), and it adds a swap device.

- **Failure modes:**
  - userdata fills up: the spill is preallocated at start and only created when it leaves
    the configured reserve free (`USERDATA_RESERVE_MB`, default 4096); it never grows later;
  - a broken spill image: when the btrfs inside it doesn't mount it is recreated (its
    content is per boot: the writeback file is new at every start, nothing is lost);
  - an I/O error while reading back a written-back page: the app that owned it gets an
    error on that page and may crash; that app's unsaved state is lost;
  - eMMC wear: only frozen, parked apps are written, there is no daily budget and no thrash
    guard yet (out of scope);
  - swap interplay with hibernation: the hibernate swap file is boot's, at a lower
    priority, and the hibernate step selects it itself; the parker refuses to act while
    pages sit on two swap devices. Hibernating with a spill attached is untested.
- **What it never touches:** partitions, the partition table, the boot chain, the
  recovery, the deployments or anything outside its spill directory on userdata.
- **Escape route:** `rc-service glassine-zram stop` (swap off, spill detached), then
  `WB_SIZE_MB=0` in `/etc/glassine/zram` or removing the service from its runlevel, and
  deleting `<prefix>/zram/` once the stop has succeeded. If it fails (the swapped pages
  don't fit in RAM), close apps and stop again, or reboot with `WB_SIZE_MB=0` first. The system boots without the service; the escape needs only a
  shell (local or SSH), nothing this feature can break.

## Acceptance

Host level (this repository, `make check`):
- the prioritizer on headless stock sway in a container with its own cgroup v2 tree:
  demote, promote, freeze, OOM score, page-out (only where the host has swap), thaw by
  datagram, thaw on an output change, park started and stopped by a thaw, `freeze NAME`,
  the desktop's own processes (long names) never frozen, exit with sway;
- the zram service with stubbed kernel files and commands: defaults, configuration and
  deploy.conf paths, the spill decisions (no mount, no room, size 0, btrfs, plain file),
  stop, park/writeback without and with a spill, usage errors;
- the parker builds and handles its usage and a dry run.

Product level (images acceptance suite): "a hidden app is frozen after the freeze delay
and comes back when shown; after the park delay it holds no zram RAM and comes back
intact". Hardware: supervised, on the first device the e-ink stage ships to.

## Out of scope

- A thrash guard (pause parking under sustained swap-in or memory pressure) and a wear log.
- Settings UI for the zram service.
- Kernel configuration and patches (the device kernel's job).
- Choosing per-device sizes (the device configuration's job).

## Success criteria

- **SC-001**: A hidden app's CPU use drops to near zero within one periodic pass after the
  freeze delay, and it repaints within one frame of being shown.
- **SC-002**: After the park delay, a parked app's anonymous memory holds no zram RAM, and
  the front app causes no writes to the user data partition.
- **SC-003**: No refresh cost: nothing here draws or causes repaints.
- **SC-004**: `make check` covers every FR above that can run without a device; what needs
  a device is listed as untested in the pull request.
