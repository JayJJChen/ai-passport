<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Koala travel assets

| Files | Integration and provenance |
| --- | --- |
| `images/koala-sprite-source.png`, `images/koala-{wave,nod,point,walk}-source.png` | Approved transparent artwork for the red, unmarked baseball-cap koala. The four motion sources are 2 × 2 sheets generated with the built-in image tool from the user's approved character; no third-party redistribution license is asserted. |
| `images/koala_frames.bin`, `images/koala_frames.bin.manifest.json` | Deterministic 20-frame, 144 × 144 RGB565A8 atlas embedded read-only in firmware: wave 0–3, nod 4–7, point right 8–11, mirrored point left 12–15, and walk 16–19. |
| `packs/western-australia/{perth-night,dunes-pinnacles,pink-lake-gorge,fremantle-rottnest,wildlife-city-sunset}.png` | Five original bright storybook illustrations generated with the built-in image tool for this fixed itinerary; they do not reuse the PDF photographs. The packer deduplicates their eleven-day use and converts them to 240 × 320 little-endian RGB565. |
| `fonts/NotoSansSC-SemiBold.ttf`, `fonts/OFL.txt` | [Google Fonts Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc), SIL Open Font License 1.1; static weight 600 derived from the original variable font with fontTools. Only the static source needed by the converters is retained. |
| `fonts/ui-24.txt`, `fonts/travel_ui_font_24.c`, `fonts/travel_ui_font_36.c` | Fixed system and itinerary character inventory at 26 px. Noto Sans SC SemiBold, 2 bits/pixel, no compression or kerning; compiled into firmware to avoid runtime binary-font parsing. |
| `packs/western-australia/cards.json`, `cards.klp`, `cards.klp.manifest.json` | Eleven-day authoring text, the five-background content pack, and SHA-256 identity. Text is validated against real 26 px glyph advances; the pack contains six files and stays below the 1 MiB partition limit. |

Reproduction uses Pillow 11.3.0, fontTools 4.60.1, and `lv_font_conv` 1.5.3:

```text
python tools/prepare_travel_assets.py --converter /path/to/lv_font_conv/lv_font_conv.js
python tools/pack_koala_frames.py verify
python tools/pack_cards.py build assets/packs/western-australia/cards.json assets/packs/western-australia/cards.klp
```

Art instructions are retained in `images/koala-art-prompts.txt`. See the
[application guide](../docs/development/koala-travel.md) for content limits,
manual USB updates, font coverage, and hardware checks.
