# Feature Specification: Screens and windows

**Feature Branch**: `shell-home`
**Created**: 2026-11-06
**Status**: Draft
**Input**: The "Glassine OS Shell" design canvas, Shell page (boards `shell.*`), and the shell spec's "Screens and windows" section.

## Context

A screen holds one to four windows. Until now the top bar opened a screen menu with per-window actions,
Overview had a separate button, and the four windows of a 2 × 2 screen shared one inner line. The design
moves every app and screen action into Overview, opened from the bar's screen button, and keeps the layout
on the dividers, each with its own position. Home becomes its own app (`GlassineOS/home` spec 001); this spec covers how the
shell shows and hides it and what happens to a screen whose last app closes.

## User scenarios

### Scenario 1 – See which screen I am on (Priority: P1)

**Given** three screens, the second with Notes and Files side by side,
**When** I look at the top bar,
**Then** its centre shows "2/3" in a small screen frame followed by the Notes and Files icons in reading
order, and it does not change when I tap from Notes into Files.

### Scenario 2 – Manage screens in Overview (Priority: P1)

**Given** four screens,
**When** I tap the screen button,
**Then** Overview opens with every screen as a preview (four per page), the current one double-framed, app
icons in four slots around each preview, and under each preview buttons to move it earlier, close it with
its apps, or move it later. Tapping a preview goes to that screen.

### Scenario 3 – Move an app (Priority: P1)

**Given** Overview is open,
**When** I tap an app's icon,
**Then** switching screens pauses, previews fade, every app's icon appears inside its screen, arrows beside
apps offer the sides the moving app can take, and Close app, Full screen (or Exit full screen) and New
screen sit in a half-circle around the moving app's icon, pointing out of the screen.
Tapping an arrow places the app; tapping another app's icon picks that app; tapping the moving app's icon,
the header's undo button or Escape cancels.

### Scenario 4 – Arrange windows on the dividers (Priority: P1)

**Given** a screen with three or four windows,
**When** I tap any divider,
**Then** every divider shows its Swap button at its middle; on a 2 × 2 screen a round crossing toggle sits
where the separate lines would meet. The buttons stay until I tap a divider again, tap elsewhere or press
Escape, so several swaps take one tap each.

### Scenario 5 – Resize each line on its own (Priority: P2)

**Given** a 2 × 2 screen,
**When** I drag the inner line of the top half,
**Then** only an outline moves while I drag, it snaps to ⅓, ½, ⅔ and to the bottom half's line, and the
windows redraw once when I let go; the bottom half's line stays where it was.

### Scenario 6 – Close the last app on a screen (Priority: P1)

**Given** I am on screen 2 of 7 and it has one app,
**When** I close that app,
**Then** the screen stays, empty, with Home over it; the bar reads "2/7" with no app icons; tapping an app on
Home fills this screen. Leaving Home any other way removes the empty screen. When it was the only screen,
Home stays until an app opens.

### Scenario 7 – Go Home and back (Priority: P1)

**When** I tap the bar's Home button (leftmost) or swipe four fingers up,
**Then** Home appears full screen over the current screen with the bar above it; tapping Home again, Escape
or opening an app hides it.

## Requirements

**Top bar**
- **FR-001**: The bar's leftmost button shows and hides Home; it looks pressed while Home is shown.
- **FR-002**: The bar's centre is the screen button: the screen's number and the screen count ("2/4") inside a
  small screen frame with a thick top edge, then the icons of its apps in reading order. No layout glyph and
  no focused-window title. It names the screen and does not change with focus.
- **FR-003**: The screen button opens Overview and looks pressed while Overview is open. There is no separate
  Overview button and no screen menu.
- **FR-004**: While a screen is empty under Home, the screen button shows its number and count with no icons.

**Overview**
- **FR-010**: Screens show as previews, four per page (two by two), paged in the header ("‹ 1/2 ›") and by
  horizontal swipe. The current screen has a heavy double frame. Tapping a preview goes to that screen.
- **FR-011**: Around each preview are four icon slots, two per side, one per quarter (top-left, bottom-left on
  the left; top-right, bottom-right on the right). A window's icon (48 px) sits in the first quarter it covers
  in reading order, so an app alone on its screen sits top left.
- **FR-012**: A full-screen app's icon has a heavy ring, its preview shows only that app, and the screen's title
  reads "<App> full screen · <others> hidden".
- **FR-013**: Under each preview: move earlier, close screen (with all its apps; unavailable for the last
  screen), move later; the arrows are faint at the ends. Moving a screen keeps the page on it. Hidden while an
  app is moving.
- **FR-014**: Tapping an app's icon starts a move: previews fade, slot icons empty, each app's icon appears
  inside its screen at its window's centre, the moving one highlighted.
- **FR-015**: The arrows offered beside an app follow the spec's table: a target screen with 1 app offers all
  four sides (the side sets the split); 2 apps, the two sides that split each app's half; 3 apps, only the
  sides that split the whole half; 4 apps none (the screen dims, its title says no room). On the moving app's
  own screen the same rules apply to the screen without the moving app, and arrows that would change nothing
  are left out; an app alone on its screen gets none.
- **FR-016**: The moving app's actions (Close app; Full screen or Exit full screen; New screen; the last two only
  when its screen has more than one window) form a half-circle around its icon that points out of the screen:
  for a window narrower than the screen, Close on the side facing the nearest edge with Full screen above and
  New screen below; for a full-width half, Close toward the top or bottom edge with Full screen left and New
  screen right. A spot within 46 px of another icon, arrow or action, or leaving the preview by more than 8 px,
  moves to the first free spot around the icon. Action buttons are solid and 44 px; arrows are dashed.
- **FR-017**: Tapping another app's icon picks that app instead; tapping the moving app's own icon, the header's
  × (shown as ↶ while moving) or Escape cancels.
- **FR-018**: The screen an app leaves closes its gap: a split half that loses a window becomes whole again, and
  a screen left empty is removed (except FR-040).
- **FR-019**: Full screen shows the app over its whole screen while the other windows stay tiled underneath,
  hidden; Exit full screen brings them back. New screen puts the app on a new screen right after its own.

**Dividers**
- **FR-020**: Every split has its own divider (a 20 px bar with a grip): the main divider between the halves and
  an inner divider inside each split half. Three windows have one inner divider, four have two; the two inner
  dividers of a 2 × 2 screen line up only when their positions are equal.
- **FR-021**: Each divider has its own position: r for the main line, ri[0] and ri[1] for the inner lines. Each
  drags on its own; only an outline moves while dragging; windows redraw once on release. A line snaps to ⅓,
  ½ and ⅔; an inner line also snaps to the other inner line.
- **FR-022**: Tapping any divider shows every divider's buttons at once, each at the middle of its divider; the
  main divider's button sits a quarter along when inner dividers meet it. Swap is on every divider: on the main
  divider it exchanges the halves (with three windows, the whole window and the pair change places), on an
  inner divider the two windows of that half. Direction sits next to Swap only on a two-window screen; its glyph
  shows the arrangement it switches to.
- **FR-023**: On a 2 × 2 screen one line runs straight through the crossing and the other axis is two separate
  lines. In divider mode a 44 px round crossing toggle sits on the through-line where the separate lines would
  meet; tapping it makes the other axis run through: the two separate lines meet at their average, the old
  through-line splits at its own position, and every window keeps its quarter. Three-window screens have no
  toggle. Swap on the through-line sits a quarter of the way along, away from the toggle.
- **FR-024**: The buttons stay until a divider is tapped again, something else is tapped or Escape is pressed.

**Home and empty screens**
- **FR-029**: The home app is a setting of the session: Home by default, any app may take its place. Everything below
  that says Home means the home app.
- **FR-030**: Home is shown over the whole current screen, under the bar, when the Home button is tapped, the Home gesture
  runs (four fingers up), or the last app on the current screen closes. The bar stays above it.
- **FR-031**: Home hides when an app opens, when the Home button is tapped again, or on Escape.
- **FR-040**: When the last app on the current screen closes, that screen stays, empty, under Home. Opening an app
  from Home fills it. Leaving Home any other way removes it. When it was the only screen, Home stays until an
  app opens (Escape and the Home button do nothing then).
- **FR-041**: Previous screen and Next screen are the gestures' names for switching screens.

**General**
- **FR-050**: Every control here is reachable with Tab and pressed with Enter or Space; Escape closes the top
  panel or cancels the move; focus ring 2 px with a 2 px offset.
- **FR-051**: No animation; nothing redraws more than once a second; dragging shows only an outline.
- **FR-052**: Copy says "screen", never workspace; every state is told apart by shape, weight, outline or a word.
- **FR-053**: Works in the three voices (White, Editorial, Stationery), in portrait and landscape.

## Contracts

| Contract | Owned / consumed | Version | Spec location |
|---|---|---|---|
| Window-model commands (the shell to the window model: place, swap, swap within a half, crossing, direction, full screen, new screen, move and close screens) | owned | 2 | this repository, README "Runtime interfaces" |
| Divider list (the window model to the shell: each divider's place, kind main/inner and half) | owned | 2 | this repository, README "Runtime interfaces" |
| The home-app rule (which app, show, hide, fill the empty screen) and launching apps | owned | 1 | this spec; consumed by `GlassineOS/home` |

## Device scope and risk

Device-neutral. No device risk: nothing flashes or touches boot, recovery or user data.

## Acceptance

- Host: the window-model tests (headless sway) cover every command above with 1–4 windows, both directions,
  the own-screen move, the crossing toggle, per-line positions and screen reorder/close.
- Host: renders of the bar, Overview (browse and move, portrait and landscape) and divider mode in all three voices.
- Hardware (supervised): taps on the screen button, Overview, a move, divider mode and a drag on the device.

## Out of scope

- Home's own content (`GlassineOS/home` spec 001).
- Settings and Packages (already built).

## Success criteria

- **SC-001**: Every action in the spec's action table (open on a new screen / this screen, go to screen, move,
  full screen, close app, close screen, reorder, swap, direction) takes at most two taps from a screen.
- **SC-002**: A move, swap, crossing flip or line drag causes exactly one panel update of the screen when it
  takes effect.
- **SC-003**: Opening Overview or divider mode updates only the panel area it covers.
