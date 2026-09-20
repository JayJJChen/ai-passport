<p align="right"><a href="koala-travel.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Koala Travel Companion

The `feature/koala-travel` application starts directly in its own 240 × 320
travel UI. It keeps the baseline hardware-test menu out of the startup and
navigation paths. The companion works offline with the embedded Shanghai card
pack, a passport stamp album, an optional custom trip, a battery gauge, and a
clock. Custom trip and stamp records are stored in NVS.

## Display, controls, and motion

The companion uses 36 px city titles, 28 px messages, and 24 px right-hand key
hints. The three key centers are y=53, 160, and 267 for UP, DOWN, and OK. The
battery gauge shows BSP state of charge or an empty gauge when unavailable.

- UP shows or cycles greetings. Entering a greeting plays one wave.
- DOWN shows or cycles exploration tasks. Each new task randomly uses a
  left- or right-pointing animation.
- OK stamps the current task. The stamp is shown for three seconds, then the
  companion returns home and nods once.
- Hold OK for three seconds to enter or leave the passport album. UP and DOWN
  browse tasks; OK returns home.
- Hold UP for three seconds to enter or leave web-sync maintenance mode.
- Hold DOWN for three seconds to enter deep sleep immediately.

The first home display waves once. A successfully synchronized trip walks for
two four-frame cycles and then waves. The same entry point is reserved for a
future real destination or background change. Motions interrupt an earlier
motion, stop on a neutral red-cap frame, and are hidden on passport and
maintenance pages. The mascot is a gray koala with the approved plain red
baseball cap, yellow backpack, and blue camera.

Backlight is 75% while active, 20% after 30 idle seconds, and 10% after 60
seconds. After two minutes without input the device enters deep sleep. Any
function-button input before sleep restores 75% and still performs its action.

## Phone web sync

Maintenance mode starts the `Koala-Travel` SoftAP and an HTTP server at
`192.168.4.1`. A phone can use the captive page to set a destination title,
one to four task strings, and the device time. A valid trip is copied into NVS,
becomes the active title/task list, shows a success message, and automatically
returns home after two seconds. Web sync does not upload a background image and
does not change the card-pack partition.

The HTTP page is local to the temporary access point. It does not require a
cloud account or Internet access. Leaving maintenance mode stops the SoftAP and
HTTP server.

## Card pack and firmware resources

The version-1 layout keeps NVS and PHY, a factory app at `0x10000` with size
`0x6f0000`, and `travel_cards` at `0x700000` with size `0x100000`. A valid pack
contains destination backgrounds, text, and the exact 28/36 px font subsets.
An invalid replacement pack falls back to the embedded Shanghai pack.

The koala animation is firmware-resident rather than copied to internal RAM.
`tools/pack_koala_frames.py` deterministically converts the approved 2 × 2 RGBA
source sheets into a 20-frame, 144 × 144 RGB565A8 atlas. Frame groups are wave,
nod, right point, mirrored left point, and walk. The atlas costs 1,244,160 bytes
of Flash.

## Validation and delivery

Run `./tools/validate.sh` with ESP-IDF 5.5.3. Host tests cover interaction and
animation timing, interruption, wraparound, frame groups, card-pack integrity,
and repository checks. `tools/render_travel.sh <pinned-lvgl-directory>` renders
the real UI and representative motion frames at 240 × 320; software rendering
does not establish physical display behavior.

The firmware gate retains the merged image, matching ELF/MAP, manifest,
`flash_args`, bootloader, partition table, and application image. Flash the
verified merged image only from `0x0` for an intentional complete refresh; its
padding can reset NVS and PHY state. Do not treat a successful build or flash
as proof of visible animation, button behavior, RF performance, or sleep power.
