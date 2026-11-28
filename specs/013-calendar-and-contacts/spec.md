# Feature Specification: Calendars and contacts — the person's store and its outside services

**Feature Branch**: `spec-013-calendar-and-contacts`
**Status**: Draft
**Input**: "The system's calendar and contacts backend of decision 0017: one local store per
person that every app is a client of, one sync engine, and one list of outside services
(Nextcloud and any CalDAV or CardDAV server), set up in Settings."
**Builds on**: GlassineOS/docs decisions [0017](https://github.com/GlassineOS/docs/blob/main/docs/decisions/0017-calendar-and-contacts-data.md)
(the store, every app a client, one sync engine) and [0006](https://github.com/GlassineOS/docs/blob/main/docs/decisions/0006-data-encryption.md)
(user data unlocked at login).

## Context

Decision 0017 puts a person's calendars, to-dos, notes and contacts into one store on the
device, served over the standard calendar and contacts protocols to every app, with one sync
engine as the only program that talks to outside servers. This spec is that backend: the
store and how apps reach it, the collections in it, the list of outside services and their
secrets, how syncing behaves (offline, conflicts, errors), how an outside service is added
and removed, and what never leaves the device.

Outside services are set up in Settings (decision 0017), whose accounts page is
[spec 014](../014-settings-accounts/spec.md). The steps of adding one (FR-030 to FR-039) are
the account service's: Settings shows each step and passes on what the person types, and the
account service does every network request of the step. Apps never set up, change or remove an
account; they read the store, and may read an account's status.

The store's server and the sync engine are third-party programs chosen by whoever packages
them (decision 0017, Consequences); this spec says what they must do together, not which
they are.

Terms used below:

| Term | Meaning |
|---|---|
| store | the person's local calendar and contacts server on the device (decision 0017) |
| client | any program that reads or writes the store: the planner, the desktop's calendar layer that GNOME-style apps read (decision 0017's EDS), Home's widgets, any app that speaks the protocols |
| item | one calendar object (event, to-do, journal entry) or one contact card |
| collection | a calendar or an address book in the store; a calendar says which kinds of item it accepts |
| on-this-device collection | a collection that is not synced anywhere |
| outside service, account | one outside calendar and contacts server and the person's sign-in there |
| linked collection | a collection in the store that is synced with one collection of an account |
| account list | the person's list of accounts, shared by every app of that person |
| account service | the system program that keeps the account list, runs the connection flow and reports status |
| sync engine | the one program that syncs linked collections with their accounts |
| default collection | for each kind of item, the collection new items go to unless an app picks another |
| app-only properties | an item's `X-` properties (decision 0017), such as the planner's ink |

## User scenarios

### Scenario 1 – On this device only (Priority: P1)

**Given** a person who has set up no account, **When** they write events in the planner and
add a contact in a contacts app, **Then** both are kept in that person's store, every client
of the person shows them, and the device makes no connection to any outside server for them,
now or later.

### Scenario 2 – Add a Nextcloud account (Priority: P1)

**Given** a person with entries on the device, **When** they start "add an account" in Settings,
pick Nextcloud and type the server's address, **Then** the system opens the
server's own sign-in page in the web browser; after the person signs in there and allows the
device, the flow goes on by itself without the person typing their main password into any
GlassineOS screen, lists the server's calendars and address books, lets the person pick which
to sync and which one new entries go to, says what will be copied to the server, and after
the person confirms, the account is in the list and its first sync runs. Every app of the
person sees the new collections at once.

### Scenario 3 – Add a server from just a domain (Priority: P1)

**Given** a provider whose calendar address the person doesn't know, **When** they pick
"Other server" and type only `example.org`, a user name and an app password, **Then** the
system finds the calendar and contacts service of that domain by the standard discovery and
goes on as in Scenario 2. **When** nothing is found, **Then** the flow says so in plain words
and asks for the full address the provider gives.

### Scenario 4 – Offline, then back (Priority: P1)

**Given** an account and no network, **When** the person writes, moves and deletes entries,
**Then** every change is kept on the device at once, and the account shows "Offline: 3 changes
waiting". **When** the network comes back, **Then** the changes reach the server within a
minute and the server's own changes arrive, without the person doing anything.

### Scenario 5 – Changed in two places (Priority: P2)

**Given** an event changed on the device and on the server since the last sync, **When** the
two changes touch different properties (the title here, the place there), **Then** the event
keeps both. **When** they touch the same property, **Then** the later change wins, the other
version is kept for 30 days and can be brought back, and the account's status says that one
entry was changed in two places.

### Scenario 6 – Sign-in no longer works (Priority: P2)

**Given** an account whose app password was revoked on the server, **When** the next sync
runs, **Then** syncing of that account stops without losing anything, the account shows
"Sign in again", and every app that shows account status shows the same words. Local changes
keep waiting. **When** the person signs in again from the account, **Then** the waiting
changes go out and syncing goes on.

### Scenario 7 – Remove an account (Priority: P2)

**Given** an account with two linked calendars, **When** the person removes it, **Then** they
choose between keeping a copy of its items on this device (the linked collections become
on-this-device collections) and removing them from this device; when changes are still
waiting to be sent, the choice says how many. Nothing on the server is changed or deleted.
The account's secret is gone from the device.

### Scenario 8 – Import a file (Priority: P2)

**Given** an iCalendar or vCard file, such as one the planner kept before it used the store,
**When** it is imported into a collection, **Then** its items are added there, each item
already present (the same identity) is left as it is, the person is told how many were
added and how many were already there, and the file is not changed. Importing the same file
again adds nothing.

### Scenario 9 – Two people on one device (Priority: P3)

**Given** two people with their own logins, **When** each sets up their own accounts, **Then**
each person's apps see only that person's store and account list, and one person's apps can't
reach the other person's store, even on the same device.

## Requirements

### The store

- **FR-001**: Each person on the device has their own store, kept with that person's data
  (inside their user data, decision 0006). A guest session has none.
- **FR-002**: The store is reachable only from the device itself, never from the network, and
  only with that person's store credentials. Each person's store has its own credentials,
  made by the system, never shown to the person and never the same as their login password.
- **FR-003**: A client in the person's session gets the store's address and credentials from
  one place readable only by that person; no app asks the person for them.
- **FR-004**: The store starts when the first client connects and is ready within 2 seconds;
  it stops when no client has used it for a while. A client never has to start it.
- **FR-005**: The store is the only program that writes the items. It keeps every property of
  an item, app-only properties included, unchanged unless a client changes them.
- **FR-006**: Every change a client makes is visible to every other client of the person
  without a restart; a client that waits for changes is told of each change as it is made.
- **FR-007**: A change is kept as soon as the store accepts it; a crash or power loss after
  that loses nothing.

### Collections

- **FR-010**: Each person's store always has one on-this-device calendar (accepting events,
  to-dos and journal entries) and one on-this-device address book. They can be renamed, not
  removed.
- **FR-011**: Each kind of item (event, to-do, journal entry, contact) has one default
  collection; at first it is the on-this-device one. The person changes it in the connection
  flow (FR-035) or on an account's page. A default must accept its kind.
- **FR-012**: A collection that is read-only on its server is read-only in the store; the
  store refuses changes to it, so clients show it as read-only.
- **FR-013**: The desktop's calendar layer (decision 0017's EDS) gets one source for each
  collection of the person, added, renamed and removed as collections are, all pointing at
  the store. It never gets an account of its own or a connection to an outside server.

### The account list and its secrets

- **FR-020**: Each person has one account list. Only Settings adds, changes, pauses and removes
  accounts, through the account service; any app of the person may read the list and each
  account's status, and sees a change at once.
- **FR-021**: An account holds: its kind (Nextcloud, other server), a name the person may
  change (by default the user name and the server's host, as `name@example.org`), the server
  address found, the linked collections, whether it is paused, and its status (FR-050).
- **FR-022**: An account's secret (an app password or a password) is kept in the person's
  secret storage, unlocked with their login, never written to disk in plain text, never shown
  again after it was entered, and never put in a log, a status, a crash report or the account
  list itself. Only the account service and the sync engine read it.
- **FR-023**: Syncing runs only while the person is logged in and their data is unlocked; a
  device with nobody logged in syncs nothing.

### The connection flow

The flow is the account service's; Settings presents it (spec 014). Each step below is a state
Settings shows, with what the person can do there.

- **FR-030**: The flow starts with the kind: Nextcloud or other server.
- **FR-031**: **Address.** The person types an address: a full address, a host, a domain, or
  for other servers also an address like `name@example.org`, from which the domain is taken.
  `https` is assumed when no scheme is given. An address without encryption is refused, with
  the reason.
- **FR-032**: **Finding.** The account service looks for the calendar and contacts service at
  that address: the address itself, then the standard discovery of the domain. When discovery
  points to a different host than the one typed, the flow names that host and asks before
  going on. When nothing is found, the flow says so and asks for the full address. When the
  server's certificate isn't trusted by the system, the flow shows its fingerprint and lets the
  person trust this one certificate for this account, or stop; a trusted certificate that later
  changes stops syncing with "the server's certificate changed".
- **FR-033**: **Sign-in in the browser.** When the server offers its own sign-in for apps (as
  Nextcloud does), the flow uses it: the account service opens the sign-in page in the
  system's web browser and waits; Settings shows that it is waiting and offers to show
  the same link as text and as a code to scan, so the person can sign in on another device;
  Cancel ends the wait. When the server grants access, the flow goes on by itself with the app
  password it was given. After 20 minutes without an answer the wait ends and says so. The
  person's main password is never typed into a GlassineOS screen on this path.
- **FR-034**: **Sign-in with a password.** When the server offers no such sign-in, the flow
  asks for the user name and a password, and says that an app password is better where the
  provider offers one. A refused sign-in says so, without saying which of the two was wrong.
- **FR-035**: **Choosing.** The flow lists the account's calendars and address books, each
  with its name, the kinds of item it accepts, and whether it is read-only. The person turns
  each on or off (all calendars and address books are on at first), and picks, for each kind
  of item, whether new items go to one of the account's collections or stay in the default
  they have; only collections that accept the kind are offered for it. A kind that no chosen
  collection accepts stays in its current default, and the flow says so in plain words.
- **FR-036**: **What leaves the device.** Before the account is added, the flow says, in one
  sentence, that every item written into the account's collections, from now on and by any app,
  goes to the named server with all its data, app-only data such as handwriting included. It
  offers to copy the items already in on-this-device collections into the account's collections
  chosen as defaults too; that is off unless the person turns it on, and the flow says how many
  items it would copy.
- **FR-037**: **Adding.** On confirm, the account is added to the list with its secret, its
  collections are linked and their first sync starts. Settings then shows the
  account's status (FR-050).
- **FR-038**: Leaving the flow at any step before Adding leaves nothing behind: no account, no
  secret, no collection.
- **FR-039**: The same server and user name can't be added twice; the flow says it is already
  there and offers that account's page.

### Syncing

- **FR-040**: The sync engine is the only program that connects to an outside server for this
  data; the account service connects only during the connection flow and to check a sign-in.
  Nothing else on the device sends the store's items anywhere.
- **FR-041**: A local change to a linked collection is sent within 1 minute when the device is
  online. Changes on the server are fetched when the network comes up, when the device wakes,
  when the person asks ("Sync now"), and every 15 minutes while the device is awake and online.
  Syncing never wakes a sleeping device.
- **FR-042**: Offline, every client works as before: the store accepts every change, and the
  changes wait, in order, until the account can be reached. Waiting changes survive restarts.
- **FR-043**: Only what changed is sent and fetched, where the server supports it.
- **FR-044**: **Conflicts.** An item changed on both sides since the last sync is merged
  property by property when the two changes touch different properties. When both change the
  same property, the change made later wins. The version that lost is kept, unchanged, for 30
  days, and the person can bring it back from the account's page. An edit on one side and a
  deletion on the other keep the edited item. No conflict deletes an item.
- **FR-045**: When a server returns an item without app-only properties the device sent, the
  store keeps the device's app-only properties, and the account's status says that this server
  doesn't keep app data, so other devices syncing with it won't get it.
- **FR-046**: An item the server refuses (too large, a kind the collection doesn't accept, the
  server's storage full) stays in the store unchanged; the account's status counts it and names
  the reason; it is tried again when the reason may have gone (on the next sync for storage,
  after the person changes the item or its collection otherwise).
- **FR-047**: A collection removed or renamed on the server is unlinked or renamed in the store.
  An unlinked collection's items stay on the device as an on-this-device collection, and the
  account's status says so once.
- **FR-048**: Pausing an account stops all syncing for it in both directions; local changes
  keep waiting. Resuming syncs at once.

### Status

- **FR-050**: Each account has one status, the same words in every app that shows it:

  | Status | Words (example) | What the person can do |
  |---|---|---|
  | synced | "Synced 5 minutes ago" | Sync now |
  | syncing | "Syncing…" (a first sync: "Syncing… 120 of 900") | — |
  | waiting | "Offline: 3 changes waiting" | — |
  | paused | "Paused" | Resume |
  | sign-in needed | "Sign in again" | Sign in again |
  | unreachable | "Can't reach example.org. Trying again later." | Sync now |
  | certificate changed | "The server's certificate changed" | Trust the new one, Remove |
  | not kept | "2 entries weren't accepted: too large" | show them |
  | changed in two places | "1 entry was changed in two places" | show the other version |

  A status that needs the person (sign-in needed, certificate changed) stays until it is dealt
  with; the others replace each other. Words never contain the secret, never a raw error code
  alone, and say what happens next.
- **FR-051**: The account service tells every app that asks when an account's status changes,
  so an app shows it without polling. It tells apps no more than once every 5 seconds per
  account during a first sync.

### Removing an account

- **FR-055**: Removing an account asks first, with two choices: keep a copy on this device (its
  linked collections become on-this-device collections, with all their items) or remove its
  items from this device. When changes are waiting, the dialog says how many will not reach the
  server. Removing never changes the server.
- **FR-056**: After removal the account's secret, its trusted certificate and its replaced
  versions (FR-044) are gone. A default that pointed to one of its collections falls back to
  the on-this-device collection of that kind, or stays on the kept copy.

### Import

- **FR-060**: An iCalendar or vCard file can be imported into one collection the person picks
  (for calendars, one that accepts the file's kinds). An item whose identity is already in the
  store is left as it is; the person is told how many items were added and how many were already
  there. The file is not changed. A file that can't be read imports nothing and says why.

### Privacy

- **FR-065**: Without an account the device opens no connection to any outside server for this
  data. With accounts, the sync engine connects only to their servers.
- **FR-066**: Nothing about the store, the accounts or their use is reported anywhere: no
  usage data, no crash reports sent off the device.
- **FR-067**: The connection flow connects only to the address the person typed and to a host
  discovery named and the person accepted (FR-032).

## Contracts

| Contract | Owned / consumed | Version | Spec location |
|---|---|---|---|
| Store access: the address on the device, how a session's client gets its credentials, the on-this-device collections, change notification | owned | 1, draft | this spec FR-001–FR-013; exact names in the README once built |
| The account service: the account list, the connection flow's states and inputs (FR-030–FR-039), status (FR-050–FR-051), Sync now, pause, resume, remove, the defaults, bringing back a replaced version, import | owned | 1, draft | this spec; consumed by Settings (spec 014); apps read the list and status only |
| iCalendar (RFC 5545), vCard (RFC 6350) | consumed | — | — |
| CalDAV (RFC 4791), CardDAV (RFC 6352), WebDAV collection sync (RFC 6578), service discovery (RFC 6764) | consumed | — | — |
| Nextcloud's sign-in for apps (Login Flow v2) | consumed | v2 | Nextcloud's developer documentation |
| The person's secret storage, unlocked at login | consumed | — | the session (not specified yet; see Assumptions) |
| The system's web browser, opened with a link | consumed | — | the session's default browser |
| The store, every app a client, one sync engine, one shared account list | consumed | — | GlassineOS/docs decision 0017 |
| User data unlocked at login | consumed | — | GlassineOS/docs decision 0006 |

## Device scope and risk

Device-neutral: every GlassineOS system with a desktop session, PCs included. User data: the
store holds the person's calendars and contacts; FR-007, FR-042, FR-044 and FR-055 protect them.

- **Failure modes:** a sync bug that deletes or overwrites items on a server the person also
  uses elsewhere (guarded by FR-044's rule that no conflict deletes, by the 30-day versions, and
  by host tests against a test server); a secret leaking into a log (FR-022, a host test greps
  every log and status); the store reachable from the network (FR-002, a host test).
- **What it never touches:** partitions, the boot chain, the recovery; outside servers beyond
  the accounts the person set up.
- **Escape route:** pause or remove the account (FR-048, FR-055); the store keeps working on the
  device without any account.

## Acceptance

Host level, against a test calendar and contacts server in a container:

- with no account, a session that writes, changes and deletes items opens no outside
  connection (network capture);
- the store refuses a connection from outside the device and one with another person's
  credentials;
- a change by one client is seen by a second client and by the desktop's calendar layer without
  a restart, app-only properties unchanged;
- the connection flow, run without Settings (its states and inputs only), adds an account
  from a full address, from a bare domain by discovery, and through a stand-in for the server's
  browser sign-in; Cancel at each step leaves nothing behind; a second add of the same account
  is refused;
- offline changes wait, survive a restart of every program, and arrive in order when the server
  is reachable again;
- both conflict kinds of FR-044 and edit-against-delete, with the replaced version brought back;
- a server that drops `X-` properties and one that refuses a large item give the statuses of
  FR-045 and FR-046, and the store keeps everything;
- a revoked password gives "Sign in again", and signing in again sends what waited;
- removing an account with each choice, with changes waiting; the server unchanged afterwards;
- importing the same file twice adds its items once;
- no log, status or crash file contains the secret.

Product level, in the images acceptance suite: add an account against a test server, write an
event in the planner, see it on the server, change it on the server, see it in the planner and
in GNOME Calendar.

Hardware: memory of the store, the sync engine and the desktop's calendar layer together, and
syncing across a sleep and wake, on a device (decision 0017's owed checks).

## Out of scope

- The Settings page for accounts: spec 014.
- Setting up accounts from inside an app.
- Accounts other than calendar and contacts servers (mail, files).
- Servers that need their own protocol instead of CalDAV and CardDAV.
- Sharing a collection with other people, and invitations to events.
- Exporting a collection to a file.
- Reminders and notifications (the reminders spec, later; it will read the store).
- Backup of the store (it is part of the person's data, backed up with it).

## Key entities

- **Store**: one per person; its collections and their items; its credentials (FR-001–FR-007).
- **Collection**: a calendar or address book; on-this-device or linked; the kinds it accepts;
  read-only or not (FR-010–FR-013).
- **Default collection**: one per kind of item (FR-011).
- **Account**: kind, name, server address, linked collections, paused, status; one entry in the
  person's account list (FR-020, FR-021).
- **Account secret**: the app password or password, in the person's secret storage (FR-022).
- **Trusted certificate**: one per account, only when the person trusted it (FR-032).
- **Waiting change**: a local change not yet on the server, in order (FR-042).
- **Replaced version**: the losing side of a conflict, kept 30 days (FR-044).
- **Connection flow**: one run of FR-030–FR-039, owned by the account service, shown by
  Settings.

## Edge cases

- **The flow runs twice at once** (two Settings windows): each run is separate; the second to add
  the same account is told it already exists (FR-039).
- **The person changes the default while items wait**: items keep the collection they were
  written to; only new items go to the new default.
- **An outside to-do or event the planner doesn't know how to show** (no date, a kind it doesn't
  use): it stays in the store unchanged and other apps show it.
- **A collection on the server accepts events but not journal entries**: journal entries can't
  default to it (FR-035); existing ones stay where they are.
- **The clock is wrong**: "later" in FR-044 is the time each side recorded; a device far off the
  server's time can lose a conflict it should have won; the replaced version is still kept.
- **The secret storage is locked or missing**: syncing waits with "Sign in again"; the store works
  on the device.
- **The person logs out during a first sync**: it stops and goes on at the next login, without
  duplicating items.
- **A very large first sync** (thousands of contacts): the store is usable during it; status
  counts progress (FR-050) at most every 5 seconds (FR-051).
- **The server is reached over a captive portal**: the answer isn't a calendar server; the status
  is "Can't reach", never a sign-in error, and nothing is deleted.
- **The same item in two linked collections of two accounts**: they are two items; nothing tries to
  merge them.

## Assumptions

- Each person on a device has their own login and user data (decision 0006); the account list,
  the store and the secrets are per person. "One list for the whole system" means one list per
  person, kept by Settings, that every app of the person reads, not one per app.
- The session offers each person a secret storage unlocked at login. GlassineOS doesn't specify
  one yet; this spec needs it.
- The system has a web browser for the browser sign-in; without one, the link and code of FR-033
  let the person sign in on another device.
- Outside servers keep app-only properties as decision 0017 asks of clients; FR-045 covers those
  that don't.
- A first sync of a few thousand items finishes within minutes on the slowest supported device.

## Success criteria

- **SC-001**: A person adds a Nextcloud account in Settings in under 3
  minutes, without typing their main password into a GlassineOS screen.
- **SC-002**: With no account, a week of normal use opens zero outside connections for this data.
- **SC-003**: Online, a change on the device is on the server within 1 minute, and a change on the
  server is on the device within 15 minutes, or at once after "Sync now".
- **SC-004**: In every conflict and failure case of the host tests, no item is lost: each is in the
  store, on the server, or kept as a replaced version.
- **SC-005**: An account added, paused or removed in Settings shows the same in every app of the
  person that reads the list within 5 seconds.
- **SC-006**: A first client gets an answer from a stopped store within 2 seconds.

## Repository

This spec is a draft in `GlassineOS/shell` because the shell holds Settings, the session's
services and the session's system files, which this backend joins. A repository of its own
may fit better: the backend is not part of the desktop shell, several repositories (shell's
Settings, the planner, Home) consume it, and the shell is installed only on e-ink images today
while this backend belongs on every GlassineOS system with a desktop, PCs included. The
packaging of the store's server, the sync engine and the desktop's calendar layer belongs to
`GlassineOS/packages` and `GlassineOS/images` in either case.

## Design boards

None of its own: the backend has no screens. Settings' boards are listed in spec 014.
