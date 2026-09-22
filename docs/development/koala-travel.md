<p align="right"><a href="koala-travel.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Fixed Western Australia Itinerary Companion

`feature/koala-travel` boots directly into a 240 × 320 offline companion for
the fixed family trip from 2 through 12 October 2026. Eleven days provide a
route, activity, lodging card, and up to four reminders. Legacy `custom_trip`
and stamp data remain untouched but are no longer read. The travel cards remain
offline-first; Wi-Fi is used only by the optional push-to-talk assistant.

## Date and controls

Automatic mode uses Perth's fixed UTC+8 zone. Dates map to Day 1–Day 11; an
invalid/pre-trip clock previews Day 1, and a post-trip date retains Day 11 with
"trip complete". Holding UP selects Auto or any itinerary day and persists the
choice. The device has no minute-level clock editor.

- UP cycles route, activity, lodging, then home.
- DOWN enters or cycles today's reminders.
- On home, press and hold OK to talk; releasing OK sends the utterance. Pressing
  OK while the assistant speaks interrupts it and starts a new utterance.
- Outside home, OK completes the visible reminder or returns home.
- Hold UP opens the date selector; hold DOWN sleeps immediately.
- Double-UP on home opens Wi-Fi provisioning.

Home shows the next unfinished reminder. Completion shows a three-second
acknowledgement, then the next item; finishing the day shows an all-done message.
Optional `HH:MM` reminder fields affect ordering when the clock is valid, but
never wake the device, ring, or play audio.

## Wi-Fi and voice

There is no wake word. With no saved network, boot starts a `Koala-*` SoftAP and
captive portal. With saved credentials, the device retries station mode and
keeps the travel UI usable offline; it does not force provisioning merely
because the network is unavailable. Double-UP explicitly re-enters the portal.

Before each utterance the device uploads a full versioned travel-state snapshot.
The server injects that state and the complete itinerary Markdown into the LLM
prompt. If state synchronization fails, that voice turn fails rather than using
stale completion data. The device UI displays only fixed states (configuring,
syncing, listening, thinking, speaking, error/offline), never arbitrary LLM
text. ASR and TTS are server responsibilities; the ESP32 transports 16 kHz mono
Opus and keeps conversation history only for the current WebSocket session.

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
render do not prove physical display, buttons, Wi-Fi provisioning, microphone,
speaker, interruption latency, sleep, or animation quality; flashing requires
separate authorization.
