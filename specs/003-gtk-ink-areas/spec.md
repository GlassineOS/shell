# Feature Specification: Pen canvases of GTK 3 apps

**Feature Branch**: `ink-module-sleep-pill`
**Status**: Draft
**Input**: "Every GTK app in the session fails to load the `glassine-ink` module that the
session names in `GTK_MODULES`. Provide that module, so GTK 3 drawing apps tell the
compositor where their canvas is."

## Context

The panel engine draws a pen stroke itself, straight from the pen's events, before the
app's own frame arrives. It does that only inside a window's ink areas. The compositor
(`sway-glassine`) learns those areas in two ways: an app announces them with the
`glassine_ink_v1` Wayland protocol (GlassineOS/protocols), or `glassine-ink-discover`
finds the canvas in the accessibility tree and sets them with the `glassine_ink` sway
command. An app's own areas win over discovered ones. Without either, a window's per-app
`ink=` setting in `/etc/glassine/eink.conf` decides for the whole window.

The session starts every app with `GTK_MODULES=glassine-ink`. This feature is that module:
it makes unmodified GTK 3 apps with a known canvas widget announce the canvas, without
changing the apps.

## User scenarios

### Scenario 1 – The pen draws on the canvas, not on the toolbar (Priority: P1)

**Given** a GTK 3 drawing app with a known canvas widget (Xournal++),
**When** its window is shown,
**Then** the window's ink area is the visible part of the canvas (clipped by the
scrolled window around it), and the pen's preview never draws on the toolbar or menus.

### Scenario 2 – The area follows the layout (Priority: P1)

**Given** such an app,
**When** a side panel opens, the window is resized or the canvas scrolls out of view,
**Then** the ink area is updated after the relayout, and only when it changed.

### Scenario 3 – Dialogs of a canvas app get no ink (Priority: P2)

**Given** an app that has shown a canvas,
**When** it opens a dialog or another window without a canvas,
**Then** that window gets an empty ink area: the pen draws nowhere there.

### Scenario 4 – Other apps are left alone (Priority: P1)

**Given** a GTK 3 app with no known canvas widget, or any app outside a compositor that
offers `glassine_ink_manager_v1` (stock sway, X11),
**When** it starts,
**Then** the module loads without an error and changes nothing; the per-app setting and
`glassine-ink-discover` decide as before.

### Scenario 5 – A self-drawn app's pages are shown at once (Priority: P1)

**Given** an app that draws itself in one window and answers a tap with a new picture
(Settings, the first-run setup), on an e-ink panel,
**When** the person taps a control,
**Then** the panel is told that the repaint is a page and shows it at once in one exact
refresh; while the person drags (a slider), the repaints are shown fast, and the exact
picture follows when the drag ends. The app has no code for it. An app that did not ask
is shown exactly as before.

## Requirements

- **FR-001**: A GTK 3 module named `glassine-ink`, installed as
  `/usr/lib/gtk-3.0/modules/libglassine-ink.so`.
- **FR-002**: Canvas widgets are recognized by GType name. The default list is `GtkXournal`;
  `GLASSINE_INK_WIDGETS=Type,Type` replaces it.
- **FR-003**: For each mapped toplevel, the areas are the union of the allocations of
  every drawable canvas widget, each intersected with all its ancestors' allocations, in
  the toplevel's surface coordinates, sent with `glassine_ink_surface_v1.set_areas`.
- **FR-004**: Updates run once per relayout (an idle callback after the toplevel's
  size allocation) and are only sent when the region changed.
- **FR-005**: Once the process has shown a canvas, a toplevel without one gets an empty
  region. Before that, toplevels without a canvas get nothing.
- **FR-006**: Unmapping a toplevel destroys its ink object (the window's default again).
- **FR-007**: Without a Wayland display or without the `glassine_ink_manager_v1` global the
  module does nothing after its registry check.
- **FR-008**: `GLASSINE_INK_LOG=1` logs every region sent, for testing.
- **FR-009**: For an app that asks for it, the module tells the compositor what a repaint
  of a window is (`glassine_ink_surface_v1.set_content`): the first repaint within 150 ms
  of a tap in that window is a page; a repaint after the finger, the pen or the pointer
  moved 8 px or more while down, or after a scroll event, is motion; every other repaint
  says nothing, also the one after the drag ends.
- **FR-010**: An app asks with `GLASSINE_CONTENT=auto:PID` in its environment, PID being
  its own process (GlassineOS/ui's Python package sets it when imported), read when an
  input arrives. `auto` alone asks for every app that inherits it. Another process's PID,
  `off` or no value: the app did not ask, and nothing about it changes.
- **FR-011**: Any app can name a window's next repaint itself with a style class on the
  toplevel: `glassine-content-page`, `glassine-content-motion`, or `glassine-content-none`
  (nothing is said, whatever FR-009 gives). The module takes the class off after that
  repaint.
- **FR-012**: Nothing is said when the compositor's protocol is older than version 4, and
  for a window drawn somewhere else than on its surface (a picture of it).

## Out of scope

GTK 4 has no module mechanism: GTK 4 apps are covered by `glassine-ink-discover` or by
their own protocol support. Brushes (`set_brush`, `create_brush`) stay with apps that
know their tools.

## Acceptance

Host test (`test/ink`): a GTK 3 app with a canvas in a scrolled window and a toolbar, run
with the module under headless `sway-glassine` (arm64, where the package is published).
The compositor's ink report must list the window with its own areas, exactly the
visible canvas; a dialog afterwards must report an empty area. On other architectures,
and with stock sway, the test checks that the module loads and stays silent.
