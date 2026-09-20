<p align="right"><a href="koala-travel.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Fixed Western Australia Itinerary Companion

`feature/koala-travel` boots directly into a 240 × 320 offline companion for
the fixed family trip from 2 through 12 October 2026. Eleven days provide a
route, activity, lodging card, and up to four reminders. Legacy `custom_trip`
and stamp data remain untouched but are no longer read. The application starts
no SoftAP, HTTP server, or phone editor.

## Date and controls

Automatic mode uses Perth's fixed UTC+8 zone. Dates map to Day 1–Day 11; an
invalid/pre-trip clock previews Day 1, and a post-trip date retains Day 11 with
"trip complete". Holding OK selects Auto or any itinerary day and persists the
choice. The device has no minute-level clock editor.

- UP cycles route, activity, lodging, then home.
- DOWN enters or cycles today's reminders.
- OK completes the visible reminder; elsewhere it returns home.
- Hold OK opens the date selector; hold DOWN sleeps immediately.
- Hold UP is intentionally reserved for a future ESP-NOW message entry point.

Home shows the next unfinished reminder. Completion shows a three-second
acknowledgement, then the next item; finishing the day shows an all-done message.
Optional `HH:MM` reminder fields affect ordering when the clock is valid, but
never wake the device, ring, or play audio.

## Motion

Normal boot or same-day wake waves once. Schedule cards wave, new reminders
point, and completion nods. First entry to Day 1, an automatic day change, or a
manual change to another day plays the full transition: background/title first,
"Day X, let's go", two four-frame walking cycles from off-screen left, then a
wave at the normal position (about 3.35 seconds). Short presses are ignored
during it, while hold-DOWN still sleeps. The last transitioned date is persisted
so the same day does not replay.

## Content, art, and fonts

The version-2 pack contains the 11-day JSON and five deduplicated 240 × 320
RGB565 backgrounds in the 1 MiB `travel_cards` partition. The bright storybook
illustrations were created with the built-in image generation tool and do not
reuse photographs from the itinerary PDF. An invalid replacement pack falls
back to the embedded Western Australia pack.

OFL-licensed Noto Sans SC SemiBold is subset at 26 px for all fixed Chinese and
English text. The generated C fonts are compiled into firmware, avoiding a large
runtime binary-font parse. Both the packer and runtime validate the real glyph
advance width and the two-line layout limit.

## Simulation and validation

`tools/render_travel.sh <pinned-lvgl-directory>` renders all eleven days,
schedule/reminder/completion states, the date selector, and first/middle/arrived/
wave transition frames using production UI code. Add
`--datetime 2026-10-08T15:30` to inject a simulator time.

Run `./tools/validate.sh` under ESP-IDF 5.5.3. A successful build and software
render do not prove physical display, buttons, sleep, or animation quality;
flashing requires separate authorization.
