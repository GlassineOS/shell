# Feature Specification: Handwriting recognizer

**Feature Branch**: `text-input-spec`
**Status**: Draft
**Input**: Turn pen strokes into text on the device with a recognizer the project trains
itself, as one self-contained program: the on-screen keyboard's writing paper (spec 006)
is its first client, but any app can use it, including apps that reverse the idea, such
as one that turns the person's own handwriting into a font.

## Context

Online recognition reads the pen's movement (points with time, pen down and up) rather
than a picture of finished writing: stroke order, direction and pauses carry most of
what tells letters apart, so a small model is enough and it can work while the person is
still writing. The pen already reports this data.

Spec 006 needs the recognizer for the writing paper and fixes the flow there (pending
text, alternatives, corrections). This spec is the recognizer itself: what it accepts and
returns, the samples it learns from, how it learns, and how other apps use it.

## User scenarios

### Scenario 1 – Writing into a text field (Priority: P1)

**Given** the keyboard's writing paper (spec 006), **When** the person writes a short
phrase and pauses, **Then** the recognizer returns the phrase and up to two alternatives
within a second, ranked with the text before the cursor in mind.

### Scenario 2 – It reads the person's hand better every week (Priority: P1)

**Given** the person has confirmed or corrected handwriting for a while, **When** the
device has been idle and charging, **Then** the recognizer has adapted to their writing,
and a recognition that failed before on their samples now succeeds more often; if an
adaptation would make it worse on their own samples, it is not used.

### Scenario 3 – Another app asks for recognition (Priority: P2)

**Given** a note-taking app with its own ink, **When** it hands a selection of strokes to
the recognizer, **Then** it gets text and alternatives back, through the same interface the
keyboard uses, with no handwriting stored unless the person confirms text in that app and
the app passes the confirmation on.

### Scenario 4 – Write your own font (Priority: P2)

**Given** an app that shows prompted text (a pangram, then single letters and digits),
**When** the person writes each prompt, **Then** the recognizer aligns the strokes to the
prompt's characters and returns one stroke group per character, which the app turns into
glyphs; the same samples, labelled by the prompt, also teach the recognizer.

### Scenario 5 – Gestures (Priority: P2)

**Given** strokes that are a strike-through, a caret or a scratch-out, **When** they reach
the recognizer, **Then** it reports the gesture and what it covers instead of reading it as
letters.

### Scenario 6 – The person stays in charge of their samples (Priority: P1)

**Given** stored samples, **When** the person opens the settings, **Then** they see how many
samples there are, from which apps, and can turn learning off, erase everything, or export
and import their samples and adaptation as one file (for a new device); nothing leaves the
device otherwise.

## Requirements

- **FR-001**: The recognizer is a separate program with a stable, versioned interface
  on the person's session; the keyboard and any app are its clients. It starts on demand
  and stops after a while without requests. It runs without a network.
- **FR-002**: Input: one or more strokes, each a list of points (x, y in a stated unit,
  time in milliseconds, pen pressure where the pen has it) between pen down and pen up;
  optional context: the text before the cursor, the language or languages to expect, the
  field's purpose, and a prompt when the text is known in advance (FR-007).
- **FR-003**: Output: up to a requested number of candidates (default three), best first,
  each with a score and its characters aligned to the strokes (which strokes, or which
  part of a stroke, made each character); or a gesture (FR-008) with the characters or
  position it covers.
- **FR-004**: Writing in progress: a client can send strokes as they are drawn and ask for
  the current reading at any time; the final reading after a pause is ready within 1 s for
  a short word on the reference device, and the recognizer uses no processor time while no
  client writes.
- **FR-005**: Engines: recognition runs in an engine behind the interface. The default
  engine is the project's own online model, trained by the project, shipped per language
  as a base model, with a personal adaptation on top (FR-009). Another engine can stand in
  through the same interface; clients don't change.
- **FR-006**: Languages: the first base model covers English (letters, digits, common
  punctuation); adding a language is adding a base model. [NEEDS CLARIFICATION: which
  other languages the first release must cover.] The
  language comes from the client's context, else from the session's keyboard layout and
  locale.
- **FR-007**: Prompted writing: when the client passes the expected text, the recognizer
  aligns the strokes to it (each character's strokes) and reports how well they fit, so
  the client can ask for a retry of a character that doesn't match.
- **FR-008**: Gestures: strike-through (deletes what it crosses), caret (insert here),
  scratch-out (discard), reported with their extent, before any text is read from the
  same strokes.
- **FR-009**: Samples and learning: a sample is the strokes, the confirmed text, the
  alignment, the language, the client that sent it and the time. Samples come only from a
  person's confirmation in a client (a picked alternative, a correction, a prompt written
  in prompted mode), never from the recognizer's own unconfirmed output, never from a
  private field. They are stored in the person's data directory, readable only by them.
  Adaptation runs only while the device is idle and charging, is checked against a
  held-back part of the person's own samples, and is kept only when it does better than
  the one in use; the previous adaptation stays available to go back to.
- **FR-010**: Control: learning on or off (on by default); erase everything (samples and
  adaptation) in one action; counts per client; export and import of samples and
  adaptation as one file, by the person only. These are offered by the settings app; the
  recognizer exposes them through its interface.
- **FR-011**: Clients and samples: any client may ask for recognition; storing samples
  from a client follows the learning setting, and the per-client counts let the person
  see and erase what each app taught. A client that reads samples (Scenario 4) gets only
  those it sent itself unless the person grants it all of them.
- **FR-012**: Base models are trained only on data the project may use and redistribute
  (data under a suitable licence, and text rendered as synthetic handwriting); the
  training recipe, the data sources and their licences ship with the models. No sample of
  any person is part of a base model.
- **FR-013**: No device names in the program; what differs per device (the pen's units,
  pressure, rate) comes from the client's stroke data.

## Key entities

- **Stroke**: points between pen down and pen up (x, y, time, pressure).
- **Reading**: candidates with scores and character-to-stroke alignment, or a gesture.
- **Sample**: strokes, confirmed text, alignment, language, client, time.
- **Base model**: per language, shipped with the program, the same for everyone.
- **Adaptation**: per person, built on the device from their samples.

## Contracts

| Contract | Owned / consumed | Version | Spec location |
|---|---|---|---|
| Recognizer session interface (recognize, stream, prompted alignment, gestures, samples, learning control, export/import) | owned (this program) | 1 | FR-001–FR-011; defined with the plan |
| Sample file format (export/import) | owned (this program) | 1 | FR-010 |
| Base model package (model, language, recipe, data sources and licences) | owned (this program) | 1 | FR-005, FR-012 |
| Writing paper and corrections | consumer (spec 006) | — | spec 006 FR-011–FR-013a |
| Settings: learning, erase, counts, export/import | consumer (the settings app) | — | spec 006 FR-008 |

## Success criteria

- A short word reads within 1 s of the pause on the reference device; no processor use
  while idle.
- On a fixed test set of the project's own recorded handwriting, the base model's
  character error rate is published per release, and does not get worse from one release
  to the next.
- After a few hundred confirmed samples from one person, the error rate on their held-back
  samples is lower than the base model's alone.
- Scenario 4's prompted mode returns one stroke group per prompted character for a
  pangram written in one go.

## Device scope and risk

Device-neutral; it needs a pen that reports positions over time.

- **Failure modes:** a bad adaptation (caught by FR-009's check, with the previous one
  kept); samples leaking from a private field (clients never send them; a host test per
  purpose); a slow engine (FR-004 budget; clients never wait on it, spec 006 FR-011).
- **What it never touches:** the network, partitions, the boot chain, system files.
- **Escape route:** learning off, erase, or going back to the previous adaptation; the
  keyboard works without handwriting.

## Acceptance

Host level: the interface against recorded stroke files (recognize, streaming, prompted
alignment, gestures); the sample rules (no sample without confirmation, none from a
private field, erase and export/import round trip); an adaptation that does worse is
rejected.

Hardware: supervised: writing on the keyboard's paper in both languages; an adaptation
after a day of use, compared on held-back samples.

## Out of scope

- Turning stroke groups into a font file (the font app's work; this program delivers the
  aligned strokes).
- Recognition of whole pages, layout or drawings (shapes, maths).
- Any recognition off the device.

## Open questions

- [NEEDS CLARIFICATION: whether the recognizer lives in this repository or gets its own
  repository, given that clients beyond the shell are expected.]
