# Feature Specification: Text input — suggestions and handwriting

**Feature Branch**: `text-input-spec`
**Status**: Draft
**Input**: Make typing on the on-screen keyboard closer to a contemporary phone keyboard,
and let the pen write text into a field: word suggestions from the text before the
cursor, and handwriting written directly above the field and turned into text.

## Context

The on-screen keyboard (GlassineOS/wvkbd, started by the session through
`glassine-wvkbd`) is a small, hackable base. Typed text reaches apps as key events; a
character that the active keymap lacks is sent through a temporary keymap. The compositor
(sway 1.10) supports the input-method protocol (`input-method-v2`) towards the keyboard
and the text-input protocol (`text-input-v3`) towards apps, which together give the
keyboard the focused field's purpose, the text around the cursor, and a popup placed at
the cursor.

The pen draws with low latency where a window has announced an ink area
(`glassine_ink_v1`, spec 003).

## User scenarios

### Scenario 1 – Suggestions while typing (Priority: P1)

**Given** a focused text field in an app that supports text input, **When** the person
types, **Then** a row above the keys shows up to three words: completions of the word
being typed or, after a space, likely next words given the text before the cursor.

### Scenario 2 – Picking a suggestion (Priority: P1)

**Given** suggestions on screen, **When** the person taps one, **Then** it replaces the
partly typed word (or is inserted after the space) followed by a space, in one step that
the app sees as one edit.

### Scenario 3 – Any character arrives (Priority: P1)

**Given** an app that supports text input, **When** the person types a character that
the keymap lacks (an umlaut from the layout, a held alternate, an emoji), **Then** the
app receives exactly that character, in every toolkit and browser the images ship.

### Scenario 4 – Private fields stay private (Priority: P1)

**Given** a password or PIN field, or a field the app marks as sensitive, **When** the
person types, **Then** no suggestions appear, nothing typed there is learned or stored,
and no handwriting is offered.

### Scenario 5 – The keyboard fits the field (Priority: P2)

**Given** a field whose app states its purpose (number, phone, PIN, email, URL),
**When** the keyboard appears, **Then** it opens on the matching layer (digits for
number/phone/PIN, a layer with `@` and `.` for email, `/` and `.` for URLs); a field
that asks for no automatic capitals gets none.

### Scenario 6 – Learning the person's words (Priority: P2)

**Given** a word the base dictionary doesn't know, **When** the person has typed or
picked it a few times outside private fields, **Then** it is suggested like a known
word. The settings show that learning is on, turn it off, and erase what was learned.

### Scenario 7 – Writing above the field (Priority: P2)

**Given** an empty text field, **When** the person taps it with the pen, **Then** a
writing area opens at once just below the field's cursor (above it near the bottom
edge), instead of the keys. The same happens from the keyboard's handwriting key in any
field. **When** the person writes, **Then** the ink appears at pen latency; after a short pause the
recognized text shows in the field as pending (underlined) text with up to three
alternatives; continuing to write, tapping an alternative, or a confirm gesture commits
it; the ink clears after it is committed.

### Scenario 8 – Editing with the pen (Priority: P3)

**Given** handwriting mode and text before the cursor, **When** the person strikes
through a word in the writing area, **Then** the last word is deleted; a caret mark
inserts a space; a scratch-out of pending text discards it.

### Scenario 8a – Correcting teaches the recognizer (Priority: P2)

**Given** pending text from handwriting, **When** the person picks an alternative, or
opens the read text with the correction control, changes it with the keys and commits
it, **Then** the corrected text goes into the field, and the ink with the confirmed text
is kept as a lesson for this device's recognizer; a short note on the paper says so.
From the first day, recognition improves with use.

### Scenario 9 – Apps without text input (Priority: P1)

**Given** an app that doesn't support text input (terminals without it, X11 apps),
**When** the person types, **Then** the keyboard works as plain keys; there are no
suggestions and no handwriting popup, and nothing is learned there.

## Requirements

- **FR-001**: The keyboard acts as the session's input method. With an active field it
  sends text as committed text (and pending text where Scenario 7 needs it), and
  deletions through the input method; keys without a text meaning (Enter, Tab, Escape,
  arrows, Backspace with nothing to replace, modifier combinations) stay key events.
  Without an active field everything is key events, as without this feature.
- **FR-002**: The keyboard shows and hides itself with the field's focus (activate and
  deactivate). The session's own show/hide commands keep working and win until the next
  focus change.
- **FR-003**: Suggestions come from the text before the cursor as the app reports it
  (bounded by the protocol, about 4 KB) plus what the person typed since. If the app
  reports no surrounding text, suggestions use only what was typed into that field since
  it was focused. When the app reports that the text changed by other means, the
  keyboard drops its own view of the word being typed.
- **FR-004**: Suggestion row: one row above the keys, up to three entries, the most
  likely in the middle; it takes no space when the keyboard has nothing to show or the
  field is private. Updating it costs no full-panel refresh.
- **FR-005**: Prediction runs in a separate, replaceable program behind a line-based
  contract (context in, ranked candidates out); the keyboard works when it is missing or
  slow (no row, no blocking). The default predictor combines a per-language base model
  with the person's learned words.
- **FR-006**: The language follows the active keyboard layout; a field whose text is
  clearly in another installed language may switch the suggestions to it.
- **FR-007**: Private fields (purpose password or PIN, or the sensitive hint): no
  suggestions, no learning, no handwriting, nothing about the field written anywhere.
  Fields with purpose terminal: no learning.
- **FR-008**: Learned words live in the person's data directory, readable only by them,
  never leave the device, and are erased by one action in the settings. Learning has a
  setting (on by default) in the settings app's keyboard page.
- **FR-009**: Field purpose selects the opening layer per Scenario 5; the
  auto-capitalization hint switches capitals after a sentence end on or off.
- **FR-010**: Handwriting mode: a pen tap into an empty text field opens the writing
  surface directly; a key on the keyboard switches between keys and handwriting in any
  field. A finger tap opens the keys as before. The writing surface is the input method's
  popup, which the compositor places at the field's cursor; it is a fixed height and the
  full width of the field's window. It announces itself as an ink area, so the pen draws
  directly. Where accessibility information gives the field's size, the surface aligns to
  the field instead.
- **FR-010a**: The writing surface reads as a sheet of paper laid over the app: the
  theme's paper colour, partly see-through over a light blur of what lies under it, so the
  ink keeps its contrast on any background; a thin edge, one guide line to write on, and
  two controls in a corner (switch to keys, put the paper away). No other decoration. The
  design is the `shell.write` boards in GlassineOS/ui.
- **FR-011**: Recognition runs in a separate, replaceable program behind a line-based
  contract (pen strokes with timing in, ranked text candidates out), on the device. It
  starts on demand and stops when handwriting mode has been unused for a while.
- **FR-012**: Pending text: the best candidate shows in the field as pending text; the
  alternatives show in the suggestion row; a pause of a configurable length (default
  0.8 s) after the last stroke triggers recognition; writing further or a confirm commits
  the pending text.
- **FR-013**: Gestures per Scenario 8 are recognized from the strokes before text
  recognition and need surrounding text for deletion; without it, strike-through deletes
  by sending Backspace for the last committed word's length.
- **FR-013a**: Learning from handwriting: an explicit pick of an alternative and a
  correction (FR-012, Scenario 8a) each store the strokes with the confirmed text as a
  training sample, in the person's data directory under the same rules as FR-008 (on by
  default, local only, one setting, one erase action, none from private fields); a
  committed best candidate that the person then edits in the field within a short time
  stores the edit as the label. Labels come only from the person's confirmation, never
  from a recognizer's own output. The recognizer adapts from these samples only while
  the device is idle and charging; how it adapts is the recognizer's own concern
  (FR-011).
- **FR-014**: Cost: no processing while the keyboard is hidden; suggestions appear within
  50 ms of a key press on the reference device; recognition of a short word within 1 s.
- **FR-015**: No device names in the code; the keyboard and the programs behind FR-005
  and FR-011 are device-neutral. What differs per device (pen device, ink area support)
  comes from what the session provides.

## Contracts

| Contract | Owned / consumed | Version | Spec location |
|---|---|---|---|
| `input-method-v2`, `input-popup-surface-v2`, `virtual-keyboard-v1` | consumed (sway as the images ship it) | sway 1.10 | — |
| `text-input-v3` support in apps | consumed (GTK 3/4, Qt 6, Firefox; Chromium and Electron with their Wayland text-input switches set by the session) | — | — |
| `tablet-v2` pen events on the writing surface | consumed (sway) | — | — |
| `glassine_ink_v1` ink areas | consumed (GlassineOS/protocols, spec 003) | 1 | — |
| AT-SPI field extents (optional, FR-010) | consumed (the accessibility bus) | — | — |
| Predictor line contract (FR-005) | owned (shell) | 1 | defined with the plan |
| Recognizer line contract (FR-011) | owned (shell) | 1 | defined with the plan |
| Learned words store and the erase action | owned (shell); the settings app uses it | 1 | FR-008 |
| Handwriting samples store (strokes + confirmed text) | owned (shell); the recognizer reads it | 1 | FR-013a |
| Keyboard input-method mode and handwriting key | consumed (GlassineOS/wvkbd) | — | — |

## Device scope and risk

Device-neutral; handwriting needs a pen and the ink area support, and otherwise the
handwriting key is hidden.

- **Failure modes:** text committed twice or a wrong word replaced (a stale view of the
  field; FR-003 drops it on outside changes); a slow predictor or recognizer freezing the
  keyboard (FR-005, FR-011: never waited on); typed text leaking into the learned store
  from a private field (FR-007; covered by a host test per purpose and hint).
- **What it never touches:** partitions, the boot chain, the recovery, system files.
- **Escape route:** turning suggestions or handwriting off in the settings, or the
  session's plain keyboard (key events only), which stays available.

## Acceptance

Host level (`make check`): headless sway with a test app that supports text input and
reports surrounding text: suggestions for typed and surrounding context, picking one
replaces exactly the partial word, characters outside the keymap arrive, private fields
show and store nothing, purpose picks the layer, an app without text input gets plain
keys, a missing or stalled predictor leaves typing working; handwriting by replaying
recorded strokes into the writing surface: placement at the cursor, pending text,
commit, gestures; a stand-in recognizer for the flow.

Hardware: supervised: typing and suggestions in GTK, Firefox and Chromium; handwriting
latency and recognition of a few short sentences; the learned store erased from the
settings.

## Out of scope

- Choosing the handwriting recognition model and how it trains (its own spec; this one
  fixes the contract, the flow and the samples it learns from).
- Swipe typing, voice input, cloud services of any kind.
- Recognition of whole handwritten pages or notes outside a text field.

## Open questions

- [NEEDS CLARIFICATION: the default recognizer — an open model trained by the project
  versus a licensed engine — and the languages it must cover first.]
