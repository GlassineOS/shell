# Feature Specification: Settings — calendar and contacts accounts

**Feature Branch**: `spec-014-settings-accounts`
**Status**: Draft
**Input**: "In Settings, the person connects their calendars and contacts to Nextcloud or to
any CalDAV and CardDAV server: Nextcloud's own sign-in in the browser, other servers by address
or just a domain, choosing which calendars and address books to sync and where new entries go;
then a list of accounts with their status, pause and remove. On e-ink: few refreshes, the pen
and the on-screen keyboard."
**Builds on**: [013 calendars and contacts](../013-calendar-and-contacts/spec.md) (the store,
the account service, its connection flow FR-030–FR-039, status FR-050, removal FR-055, import
FR-060); [006 text input](../006-text-input/spec.md) (field purposes, private fields, writing
above a field); [012 the size of the interface](../012-ui-size/spec.md); GlassineOS/docs
decision 0017 (outside services are set up in Settings).

## Context

Decision 0017 says outside calendar and contacts services are set up in Settings, and spec 013
keeps the person's account list and runs every step of adding an account. This spec is the
Settings side: a page with the person's accounts, the pages of adding one, an account's page,
and importing a file. Settings shows each step of spec 013's flow and passes on what the person
types; it makes no network request itself.

Nobody else sets up an account. An app that wants to point the person here opens Settings on
this page (Settings' page argument, README "Runtime interfaces").

Terms are spec 013's (store, collection, on-this-device collection, account, account list,
account service, default collection, status, connection flow). Also:

- **Accounts page**: the Settings page that lists the person's accounts.
- **Account page**: the page of one account.
- **Add pages**: the pages of adding an account, one per step of spec 013's flow.

## User scenarios

### Scenario 1 – Connect Nextcloud (Priority: P1)

**Given** Settings and no account, **When** the person opens Accounts, **Then** the page says
"Your calendars and contacts are kept on this device only." with Add an account. **When** they
tap it, pick Nextcloud, type `cloud.example.org` and tap Continue, **Then** the browser opens
the server's sign-in page, and Settings shows one still page: "Sign in in the browser. Settings
goes on when you're done." with Show link and Cancel. **When** the person has signed in and
allowed the device, **Then** Settings, in front again, lists the server's calendars and address
books, all on, and "New entries go to" with a choice per kind; a line says which kinds no
chosen calendar takes and that those stay on this device. **When** they tap Continue, **Then**
one page says that everything written in these calendars and address books, by any app,
handwriting included, goes to `cloud.example.org`, with a switch "Also copy the 230 entries
already on this device", off. **When** they tap Start syncing, **Then** the account page shows
"Syncing… 0 of 230", later "Synced just now", and the planner and GNOME Calendar show the new
calendars.

### Scenario 2 – Another server by its domain (Priority: P1)

**Given** the add pages, **When** the person picks Other server and types `example.org`, a user
name and an app password, **Then** Settings shows "Looking for the calendar at example.org…" and
goes on to the choice of calendars. **When** nothing is found, **Then** it says "No calendar
server found at example.org. Enter the full address your provider gives." under the address,
keeping what was typed.

### Scenario 3 – The account list (Priority: P1)

**Given** two accounts, one synced and one whose app password was revoked, **When** the person
opens Accounts, **Then** each row shows the account's name and its status ("Synced 5 minutes
ago", "Sign in again"), the second with its action on the row. **When** they tap Sign in again,
**Then** the add pages run from the sign-in step for that account, and its waiting changes go
out after.

### Scenario 4 – Pause, resume and remove (Priority: P2)

**Given** an account, **When** the person turns off Sync on its page, **Then** the status is
"Paused" and every app keeps working on the device; turning it on syncs at once. **When** they
tap Remove account, **Then** a card asks "Remove this account?" with Keep a copy on this
device, Remove from this device and Cancel; with changes waiting it says "3 changes haven't
reached the server yet". The server is not changed.

### Scenario 5 – Changed in two places (Priority: P2)

**Given** an account whose status is "1 entry was changed in two places", **When** the person
taps it, **Then** a page shows the entry's two versions side by side, as text, with Use this one
under each; picking the other version brings it back.

### Scenario 6 – Import a file (Priority: P3)

**Given** an iCalendar or vCard file in the person's files, **When** they tap Import a file on the
Accounts page, pick it and pick a collection, **Then** Settings says "120 added, 4 were already
there" (spec 013 FR-060).

### Scenario 7 – Opened from an app (Priority: P3)

**Given** an app that tells the person their calendar isn't synced, **When** it opens Settings on
the accounts page, **Then** Settings opens there, as with any page argument.

## Requirements

### Where it is

- **FR-001**: Settings has a tab Accounts (calendars and contacts) with the accounts page; the
  account page, the add pages, the version page and the import page are its sub-pages, and Back
  on each returns to its parent. The page argument `accounts` opens it.
- **FR-002**: The accounts page lists the person's accounts in the account list's order, each row
  with its name, its status in spec 013's words (FR-050), and the status's action as a button
  when there is one. Below: Add an account, then the on-this-device calendar and address book
  with the number of entries in each, then Import a file. Without accounts the list is the one
  sentence of Scenario 1.
- **FR-003**: The page shows changes to the list and to statuses as they happen, without the
  person leaving and coming back (spec 013 FR-051), redrawing only the row that changed.

### Adding an account

Each step of spec 013's flow is one add page; Back returns to the step before with what was
typed kept (the password only while the flow runs); Cancel, closing Settings or leaving the
pages adds nothing (spec 013 FR-038).

- **FR-010**: **Kind.** Two rows: Nextcloud, "Sign in with your Nextcloud server's own page",
  and Other server, "Any calendar and contacts server (CalDAV, CardDAV)".
- **FR-011**: **Address.** For Nextcloud, one address field with an example in it
  (`cloud.example.org`) and Continue. For Other server, the address (`example.org`, or
  `name@example.org`), the user name and the password with Show password, one line saying that
  an app password from the provider is better than the main password where it exists, and
  Continue. Continue is greyed until the needed fields hold something.
- **FR-012**: **Finding.** "Looking for the calendar at example.org…", in words, with Cancel; no
  moving mark. When discovery points to another host, the page names it with Continue and Back.
  An untrusted certificate shows its fingerprint in a monospaced face, split into groups, with
  Trust this certificate and Back.
- **FR-013**: **Browser sign-in.** The page of Scenario 1, with Show link and Cancel. Show link
  turns the page, in its place, into the link as text and a code to scan with another device,
  for when there is no browser or the person prefers a phone. When the server grants access,
  Settings goes on to the next step by itself; when the person comes back before that, the page
  is still waiting. Settings never shows the server's sign-in inside itself. When the wait runs
  out (spec 013 FR-033), the page says so with Try again.
- **FR-014**: **Sign-in errors** show under the fields they are about, in spec 013's words with
  what to do next; a refused password never says which field was wrong.
- **FR-015**: **Choosing.** Two lists, Calendars and Address books, each row a switch with the
  collection's name, the kinds it takes in a short note ("events and to-dos") and "read-only"
  where it is; then "New entries go to", one row per kind (events, to-dos, notes, contacts), each
  offering only the collections that take that kind plus On this device. The first chosen
  collection that takes a kind is preselected for it. A kind no chosen collection takes shows On
  this device and the line "This server's calendars don't take notes; notes stay on this
  device." Long lists fold and page like every Settings page; nothing scrolls.
- **FR-016**: **What leaves the device.** One page with the sentence of spec 013 FR-036 naming the
  server, the switch "Also copy the N entries already on this device", off, and Start syncing.
  Nothing has been added before this tap.
- **FR-017**: After Start syncing, Settings shows the new account's page (FR-020). Trying to add
  an account that exists says so on the address page with Open that account (spec 013 FR-039).

### An account's page

- **FR-020**: The account page holds, from the top: the status with its action; the name, which
  the person can change; the server address; Sync (on, or off for paused) and Sync now; the
  calendars and address books with their switches (as FR-015); "New entries go to" (as FR-015);
  the waiting changes and the entries not accepted, each a count one tap from its list; Remove
  account.
- **FR-021**: Turning a collection off asks first when it holds entries, with Keep a copy on this
  device, Remove from this device and Cancel; turning one on links it and syncs it at once.
- **FR-022**: Remove account opens the card of Scenario 4 (spec 013 FR-055). Afterwards the
  accounts page shows without the account; a default that fell back to On this device is said
  once on that page.
- **FR-023**: The version page (Scenario 5) lists the entries changed in two places (spec 013
  FR-044), each with both versions as text, the time each was made, and Use this one; Settings
  shows the text of each version, not app-only data such as ink, and says that an app's own data
  goes with the version chosen.
- **FR-024**: The list of entries not accepted shows each entry's title, its collection and the
  reason in spec 013's words; it changes nothing.

### Import

- **FR-030**: Import a file opens the system's file chooser for iCalendar and vCard files, then a
  choice of collection (only collections that take the file's kinds; for contacts only address
  books), then the result in one line (spec 013 FR-060). A file that can't be read says why and
  imports nothing.

### Typing, the pen and the panel

- **FR-040**: The fields are typed. A tap on a field opens the on-screen keyboard with the field's
  purpose (an address, a user name, a password; spec 006). The page keeps the focused field and
  its Continue above the keyboard. The password field is a private field (spec 006 FR-007): no
  suggestions, no learning, no handwriting reading.
- **FR-041**: Writing above the address and user name fields with the pen works where the system
  offers it (spec 006); there is no handwriting-only field.
- **FR-042**: Every add page is a new page, shown in one refresh; nothing animates; waiting is
  said in words. A status or progress change redraws only its line, during a first sync at most
  once every 5 seconds (spec 013 FR-051). The pages lay out for Settings' window (spec 012
  FR-020), fold and page as every Settings page does, and follow the voice.
- **FR-043**: None of these pages needs an administrator: accounts are the person's own.

## Contracts

| Contract | Owned / consumed | Version | Spec location |
|---|---|---|---|
| The account service: list, flow states and inputs, status, pause, resume, remove, defaults, replaced versions, import | consumed | 1, draft | spec 013 |
| Settings' page argument `accounts` | owned | 1 | this spec FR-001; README "Runtime interfaces" |
| On-screen keyboard: purposes, private fields, writing above a field | consumed | — | spec 006, `GlassineOS/wvkbd` |
| Interleaf and the voices, Settings' look | consumed | current | `GlassineOS/ui` |
| The system's file chooser | consumed | — | the session's portal |
| Outside services set up in Settings | consumed | — | GlassineOS/docs decision 0017 |

## Device scope and risk

Device-neutral. User data: these pages copy the person's entries to a server they name; FR-016
says so before anything leaves. Turning a collection off and removing an account ask first and
offer to keep a copy (FR-021, FR-022). No system file, partition or other person's data is
touched.

## Acceptance

Host level (`make check`, `test/settings`), with a stand-in account service:

- renders of the accounts page with no account and with an account in each status of spec 013
  FR-050, of each add page, of the account page, the remove card, the version page and the import
  result, in each voice, portrait and landscape, in each fitted window size, and the address and
  password pages with the keyboard open;
- driving the add pages through both kinds: Nextcloud with a stand-in browser sign-in (granted,
  cancelled, timed out, via Show link), Other server by domain, not found, another host, an
  untrusted certificate, a refused password, an existing account; Cancel at each step leaves
  nothing in the stand-in service;
- the password field reaches the keyboard as private, for each keyboard purpose test of spec 006;
- an account added by the stand-in from outside Settings shows on the open accounts page, redrawing
  only its row;
- Settings opened with `accounts` shows the accounts page;
- Settings makes no network connection itself (network capture with the stand-in service).

Product level: spec 013's images scenario, run through these pages.

Hardware: refresh counts of the add pages and the accounts page during a first sync on a device,
the keyboard over the address field, supervised.

## Out of scope

- The store, syncing, conflicts and the flow's logic (spec 013).
- Setting up accounts from inside an app.
- Accounts for anything but calendars and contacts.
- Exporting a collection, sharing a collection with other people.

## Key entities

- **Account row**: name, status, action (FR-002).
- **Add page**: one step of spec 013's flow, with its fields and buttons (FR-010–FR-016).
- **Account page**: one account's settings (FR-020).
- **Version page**: the two sides of a conflict (FR-023).

## Edge cases

- **The browser sign-in finishes while Settings is hidden**: Settings shows the next step when it
  is in front again; the account service holds the result.
- **No browser on the system**: Show link is the way, and the page says so.
- **Two Settings windows add the same account**: the second is told it exists (FR-017).
- **Many collections**: the lists fold and page (FR-015); rows keep Settings' tap target size.
- **The keyboard covers Continue in a small window**: the page shows the focused field and
  Continue above it, on a page of their own if needed (FR-040).
- **An account removed while its page is open**: the page returns to the accounts page and says
  the account was removed.
- **A collection turned off on the server while it is on here**: the row goes, and the status's
  one-time note says its entries were kept on this device (spec 013 FR-047).

## Assumptions

- Spec 013's account service exists wherever Settings is installed.
- The system has a file chooser for Import; without one, Import a file is not shown.
- Settings' page argument works as README "Runtime interfaces" describes.

## Success criteria

- **SC-001**: A person new to it connects a Nextcloud account in under 3 minutes without typing
  their main password, and an Other server account from a domain in under 4.
- **SC-002**: Every add page costs one panel refresh; a status change costs one refresh of its
  row.
- **SC-003**: Cancel at any step leaves no account, secret or collection (host test).
- **SC-004**: Settings itself opens no network connection for accounts (host test).

## Design boards

Needed in GlassineOS/ui `designs/shell` before implementation, in Settings' look (and the fit to
each window, as the `fit.*` boards): `settings.accounts` (none, and several in each status),
`settings.accounts.add-kind`, `settings.accounts.address` (Nextcloud, and Other server with user
and password; each with the keyboard open), `settings.accounts.finding` (with another host),
`settings.accounts.certificate`, `settings.accounts.browser` (waiting, Show link with the code,
timed out), `settings.accounts.choose` (with the "stays on this device" line),
`settings.accounts.leaves` (what leaves the device), `settings.account` (each status),
`settings.account.remove` and the turn-off card, `settings.account.versions`,
`settings.accounts.import`. Portrait and landscape.
