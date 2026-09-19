<p align="right"><a href="koala-travel.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Koala Travel Companion, version 1

This application lives on `feature/koala-travel`, based on upstream commit
`31759c4d63dd0d0d6580d6f74639ed5315bcd2c3`. The hardware-test baseline and
other application worktrees remain separate. The companion starts directly at
the Shanghai waterfront, with a fixed koala, local greetings, and brief
observation invitations. No scores, deadlines, cloud account, microphone,
automatic location, or live weather are required.

## Display and controls

The native display is 240 × 320. Its physical active-area dimensions are not
specified in the available hardware contract. Semibold text uses 36 px city
titles, 28 px companion messages, and 24 px right-hand hints and parent settings.
Each companion message has at most two lines and five Unicode characters per
line. Text is rendered on solid cream backgrounds. Do not squeeze longer text
by reducing these font sizes. The three hint centers are y=53, 160, and 267,
corresponding to the physical UP, DOWN, and OK keys across the full height.
The battery gauge uses actual BSP SOC, or a hollow gauge when unavailable.
The backlight dims to 20% after 30 idle seconds and to a permanently visible
10% after 60 seconds. It never turns off automatically: a completely black
screen means the user powered the device off. Any function-button press restores
75% brightness and performs its normal action immediately. Active, bounded
parent provisioning keeps its instructions at 75%. This is backlight control,
not CPU deep sleep.
Software rendering proves layout/glyph selection only; physical readability,
outdoor contrast, and final Chinese rendering require device observation.

- UP: cycle through a few local greetings.
- DOWN: show/cycle one observation invitation.
- OK: acknowledge; show encouragement, then return home after three seconds.
- Hold OK for three seconds: enter/leave parent settings.
- In settings: UP/DOWN select; OK confirms.
- Settings offer Wi-Fi setup, confirmed credential removal, destination selection,
  and return. Shanghai is the sole shipped destination.

## Parent Wi-Fi setup

Mini-program provisioning has been reported unusable on this derivative firmware.
Compatibility investigation is deferred; this version should be used offline.
The shipped card text uses a generic greeting without a child's name.

Use the existing FoloToy BLUFI companion mini program. Its exact search name is
linked in the [provisioning guide](engineering/wifi-provisioning.zh_CN.md#mini-program-name).
The screen displays its two-part name and the current setup state. The device
advertises `BLUFI_Koala`. The reference implementation was inspected at commit
`9c039cc5127f22072afa83bedb7fa3d8efe635ad`; only networking/security patterns
are reused, never its demo screens, BSP, or older partition configuration.

The parent enters the SSID/password on the phone for a 2.4 GHz network or phone
hotspot. Save only one successfully connected network. A failed replacement
does not replace the last working credentials. Startup tries the saved network
for a bounded ten-second period without blocking the homepage; unsuccessful
attempts release Wi-Fi resources. Pairing lasts at most two minutes, and its
Bluetooth resources are released after completion/exit. No credentials are
logged. Failure or no configuration leaves all exploration available offline.
Wi-Fi association/DHCP is not proof of Internet access. Internet-dependent
features, time synchronization, and phone-supplied location are future work.
Compatibility with the mini program and the intended phone/hotspot needs device
testing; the code/build does not establish it.

## Card packs without application recompilation

Version 1 deliberately implements USB content replacement, without a filesystem,
phone upload page, OTA program updater, or extra cloud service. A `.klp` pack
holds a versioned manifest, one RGB565 background per destination, and 28/36 px
binary font subsets generated for that pack's exact Chinese text. The fixed
144 × 144 koala sprite and 24 px system font stay in firmware.
Existing greeting/exploration/acknowledgement interactions can receive new text
and backgrounds without compiling C. New interaction types still require a
firmware update. Parent settings select among the destinations in a pack.
Destination selection is manual and starts at the first place after restart.

Prepare content by copying `assets/packs/shanghai/cards.json` and its background
into a new folder. Use 1..6 unique destinations, 1..20 greetings/tasks per
destination, title length 1..4, and the two-line/five-character message limits.
Pack capacity is 1 MiB; combined font-file budgets are 16 KiB at 28 px and
4 KiB at 36 px. Large content should be split into separate packs.

Use Python with Pillow 11.3.0 and fontTools 4.60.1, Node.js, and the official
`lv_font_conv` 1.5.3 converter. For example, from the application root:

```text
python tools/pack_cards.py build assets/packs/shanghai/cards.json build/cards.klp --converter /path/to/lv_font_conv/lv_font_conv.js
python tools/pack_cards.py verify build/cards.klp
python tools/update_card_pack.py build/cards.klp
```

The last command defaults to verification only and never opens a port. Its
`--port COMxx --flash` mode requires explicit authorization for the intended
device. It reads the device partition table, validates the content partition,
writes only `0x700000`, and checks written bytes. It does not erase the chip or
write the application/NVS. USB update resets the device and replaces the entire
content pack; it is not an atomic two-slot update. An invalid/interrupted pack
falls back to the embedded Shanghai pack at startup. CRC detects corruption;
it is not authentication. Accept only locally built/trusted packs.

The derived layout retains NVS/PHY, a factory app at `0x10000` of size
`0x6f0000`, and `travel_cards` at `0x700000` of size `0x100000`.
The complete verified image includes the default pack. Initial installation
changes the partition layout and may reset stored data; follow the
[flashing policy](engineering/firmware-layout.md#flashing-and-stored-data).
Later content-only updates use the compatible version-1 layout.

## Validation

Run `./tools/validate.sh` on the build host with ESP-IDF 5.5.3. Added host checks
cover interaction transitions, timer wraparound, parent confirmation, physical
key alignment, card readability limits, and pack CRC/truncation/overflow/overlap.
`tools/render_travel.sh <pinned-lvgl-directory>` additionally renders the actual
UI code at 240 × 320 using the same LVGL and the real generated fonts. This is
software rendering, not a hardware test.

Retain the merged BIN, matching ELF/MAP, manifest, `flash_args`, partition table,
bootloader/app images, and matching `.klp` content with SHA-256/source identity.
No Git commit, push, publishing, or USB write occurs without user authorization.
