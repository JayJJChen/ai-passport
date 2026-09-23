<p align="right"><a href="koala-travel.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# A Western Australia companion for a child

The application is a 240 × 320 offline itinerary companion for a seven-year-old.
It introduces where the family will go, what they will do, and what is interesting
there during 2–12 October 2026. The detailed return connections on 13 October
belong in voice knowledge; they do not add a twelfth device day.

## Screens and controls

The home screen always introduces the day's theme. UP cycles through route,
things to do, lodging, and home. DOWN opens the day's one to three activities
and moves to the next activity. OK on an activity records it as completed; it
does not toggle a completed item back to pending. Already completed activities
remain available to browse. The interface distinguishes synchronized progress,
an offline record awaiting synchronization, a skipped activity, and a conflict.

On home, click OK to prepare a voice turn. Speak after the cue, then click OK
again to send; clicking during preparation cancels it, and clicking during a
reply interrupts the reply and starts a new turn. Recording ends automatically
after 20 seconds; a reply still incomplete after 60 seconds ends with a voice
error. Hold UP to select automatic date or a preview day, hold DOWN to sleep,
and double-click UP on home to configure Wi-Fi. After five minutes, setup mode
no longer prevents automatic sleep; wake the device to configure it again.
Previewing a day does not mean the family is at that location. Perth time is UTC+8.

There are no packing, document-checking, hydration, scores, deadlines, or
all-activities-required messages. Optional itinerary activities can be skipped.
The home screen is not replaced by a checklist or a completion score.

## Child-friendly content and pictures

The authoring source is `assets/packs/western-australia/cards.json`, version 3.
Each activity has a stable ID, a short two-line title, and an optional flag.
The same source exports the service's activity catalogue; `trip_id`,
`content_version`, activity identities and ordering must match at delivery.
Never infer a new activity's completion from an old reminder index.

Eleven original storybook scenes follow the eleven travel days. The red-cap
koala stays the companion. On 9 October its compact 96 × 96 layout leaves a
visible quokka alongside it; other scenes retain the normal companion layout.
Inspect actual 240 × 320 rendered UI and animation frames, not only source art.

The content partition is 2 MiB at `0x600000`; the factory application starts at
`0x10000` and is `0x5f0000` bytes. Backgrounds retain RGB565, with a complete
embedded fallback pack. A device with the old 1 MiB layout must reject a direct
new card-pack update. It needs the matching firmware and partition-table
migration first. A full merged flash can reset NVS; preserving Wi-Fi/settings
requires a reviewed segmented-flash procedure. Building does not authorize flashing.

Fonts are compile-time OFL Noto Sans SC subsets. Regenerate them after text
changes and check glyph coverage and real widget layout.

## Conversation and progress ownership

The voice service is a child's travel companion. It normally answers in one
to three natural sentences, and expands when asked. The itinerary's current
plan, optional stops and unconfirmed arrangements remain distinct. Order
management credentials and historical itinerary revisions do not belong in
the spoken knowledge.

Each turn first performs a bounded activity decision using the existing text
model interface, then validates and persists permitted activity changes, then
streams the natural reply. This adds a short model request before the reply.
Only a successful write permits an acknowledgement such as "I've recorded it".
Questions, future plans and another person's experiences do not establish
completion. Ambiguous statements prompt a clarification. Explicit corrections
and skipped activities are supported.

The decision prompt and reply prompt have separate stable versioned prefixes.
Date, selected page, authoritative progress and this turn's result form one
replaceable dynamic section. Decision JSON is never sent to speech or retained
as ordinary dialogue. Main dialogue is bounded to eight complete rounds and
12 KiB of UTF-8 history; reconnecting can clear chat history without clearing
persistent activity progress.

Device telemetry and activity progress have different authorities. The device
owns its current page/date/battery snapshot; the service owns synchronized
activity progress. A stale device snapshot must never replace server progress.

## Offline records and synchronization

An offline OK press immediately records a local completion and persists an
idempotent operation. Repeated presses on the same pending activity coalesce.
Pending operations survive reboot and remain distinguishable from server
acknowledgements. Before speech and after network recovery the device submits
pending operations; it refreshes progress after replies and interruption.

Operations identify the activity, desired state, content version, operation ID
and expected activity revision. The service validates them in a transaction.
Repeated operations or the same already-recorded state succeed idempotently.
A fresh accepted operation advances the activity revision, even when it confirms
the same state; replaying the same operation ID does not. An explicit voice
confirmation can therefore resolve a device conflict without changing the state.
A conflicting different state keeps the server result and calls for an
explicit correction rather than silently overwriting either intention.
A catalogue-version mismatch pauses writes and retains unsent records.

Only the UI task changes UI or its owned state; network/audio callbacks queue
events. Old habit-reminder completion bits are not migrated to child activities.
Date selection, animation bookkeeping and independent Wi-Fi settings are preserved.

## Validation and delivery

Run `./tools/validate.sh` in ESP-IDF 5.5.3. `tools/render_travel.sh` renders
the production UI using pinned LVGL; inspect all eleven days and the compact
quokka scene in entry, idle, activity, pending, skipped, conflict and voice states.
Service tests must cover real outgoing mock-LLM requests, both prompt prefixes,
recording/non-recording cases, offline replay, conflicts and cancellation.

Preserve a source identity plus matching merged BIN, application BIN, ELF, map,
partition image, flash arguments and archive manifest. Verify downloaded hashes.
Report build, host tests, device tests and unverified items separately.
Mock audio proves neither speech-recognition quality nor real speaker/microphone
quality. A Pi LAN service also needs a separate travel-network connectivity
check before it can be relied on from a mobile hotspot abroad.
