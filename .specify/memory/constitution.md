# GlassineOS shell Constitution

This repository follows the GlassineOS organization principles (I–VIII). They come from
the `glassine` Spec Kit preset in `GlassineOS/.github` and are not edited per
repository. Repository-specific principles go in the last section.

## Organization principles

### I. Honest framing

GlassineOS is a hobby project, written largely with AI coding agents and not reviewed by
subsystem experts. Every README says so. Specs, plans and commit messages state what was
verified and how (host test, emulator, which device and date), and say "untested" where
it applies.

### II. Safety before features

Anything that flashes, partitions, touches the bootloader, recovery, updates or user data
states its failure mode and its escape route before it is implemented. If the escape route
depends on what the change could break, the change needs explicit maintainer approval.
Device-affecting steps never run without the maintainer's go.

### III. Nothing vendor-owned, nothing personal

No vendor firmware or binaries, device dumps, decompiler output, keys, serial numbers, MAC
addresses or local paths in any repository. Firmware comes from the user's own device at
install time. The safety scan must pass.

### IV. Contracts are owned and versioned

Every interface between repositories (protocols, file formats, configuration, D-Bus or
socket APIs) has one owning repository that specifies and versions it. Consumers pin the
version they support and adopt new versions through their own spec. Breaking changes
bump the major version.

### V. Test at the right level

Libraries and daemons test their own behaviour in their own repository (host tests, no
device needed where possible). End-to-end behaviour of the assembled system is specified
and tested only as product acceptance in the `images` repository, as high-level
behavioural scenarios on hardware, never repeating library unit tests.

### VI. Device-neutral by default

No device constants outside device packages and device configuration. Code reads device
facts from configuration so a second device needs no changes outside its own packages.

### VII. E-ink first

Every user-facing change considers refresh cost, ghosting and latency on e-ink. Animations
and continuous redraws need a reason. Latency or power trade-offs are checked by feel on
the device, not by benchmark alone.

### VIII. Upstream and licenses

Our code is GPL-2.0-or-later unless the repository says otherwise; fork commits use the
upstream license. Nothing is submitted upstream unsolicited. AI-assisted commits carry an
`Assisted-by:` or `Co-Authored-By:` trailer; agents never add `Signed-off-by`.

## Repository principles

### Contracts

- **Owned:** the names in README's "Runtime interfaces": the window model's commands and the lists it
  sends the shell, the shell's requests, the `/etc/glassine` files the session reads, the theme names.
  Other repositories use them, so a rename changes the consumers in the same release.
- **Consumed:** sway's IPC (the version the images ship), the e-ink daemon's control FIFO from
  `GlassineOS/epd`, the on-screen keyboard from `GlassineOS/wvkbd`, freedesktop `.desktop` files.

### Tests

Every change runs `make check` and extends the tests for new behaviour. The tests run on the host
(headless sway in a container, offscreen renders), so a pull request says separately what ran on a
device, supervised, and what didn't.

### Specs

New features start with `specs/###-name/spec.md` (`/speckit-specify`), written before any plan or
code. Only `spec.md` is committed; plans, research and tasks stay local. Bug fixes and small
changes can go straight to a pull request. Features merged before this constitution have no spec
and get one only when reworked. An implementation that departs from its spec or the design says so
before it is built.

### Releases and hardware

No releases of its own: the images package this repository. It never flashes or writes a device's
partitions; device runs are supervised and change only files of the running session.

## Governance

The organization principles change only in `GlassineOS/.github` (preset version bump);
repositories pick changes up through their `.glassine/org` submodule. Repository
principles change by pull request in this repository. Cross-repository decisions are
recorded as decision records in `GlassineOS/docs`.

**Version**: 1.0.0 | **Ratified**: 2026-11-06 | **Last Amended**: 2026-11-06
