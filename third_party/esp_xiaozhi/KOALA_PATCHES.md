# Local esp_xiaozhi patch

This component is copied from Espressif's `espressif/esp_xiaozhi` 0.1.2
(upstream `esp-iot-solution` commit
`5d75f3f0dc499d9ed4b69284a3741187c2b75a70`). Its original
Apache-2.0 license is in `license.txt`. `main/idf_component.yml` selects this
copy with `override_path`.

The only upstream source change is in `src/esp_xiaozhi_websocket.c`:

- Bound WebSocket text and binary sends to 3000 ms per lock wait and transport
  write, so a stalled hotspot cannot hold the voice sender indefinitely.
- Treat a short write, including a zero-byte timeout, as a send failure. A
  partial command or audio frame must never appear to have been sent.

When updating the upstream component, reapply this patch and run
`./tools/validate.sh`. The 3000 ms value is a starting point; real hotspot
disconnect and weak-network behavior still needs device testing.
