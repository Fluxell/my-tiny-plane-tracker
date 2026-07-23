# my-tiny-plane-tracker

Show nearby aircraft on a tiny round display. An ESP32-S3 fetches live flight
data for your location and renders it as directional triangles + callsigns
on a 1.28" circular LCD.

## Hardware

- ESP32-S3 + Waveshare 1.28" round 240×240 GC9A01 display
  (ESP32-S3-LCD-1.28 or compatible)
- Two momentary push switches, wired to GND with the board's internal
  pull-ups (see [buttons.md](buttons.md) for GPIO pins and behavior)

## Features

- Nearby aircraft pulled from [ADSB.fi](https://adsb.fi) and drawn as
  triangles (pointing along their track) with callsigns
- Background: plain dark rings, or live Stadia Maps tiles centered on your
  location
- Auto-zoom: automatically expands/shrinks the display range to keep a
  configured number of aircraft in view
- Two physical buttons for manual range control, restart, entering the
  config portal, and toggling auto-zoom — see [buttons.md](buttons.md)
  for the full tap/hold/combo reference
- Web-based config portal (WiFi AP) for first-time setup — no code changes
  needed to configure WiFi, location, refresh rate, range, or background

## Building

This is an Arduino IDE project (no make/cmake).

1. Install the ESP32 Arduino core and these libraries via Library Manager:
   - `TFT_eSPI` by Bodmer
   - `ArduinoJson` (v6 — for v7, rename `DynamicJsonDocument` to `JsonDocument`
     in `flights.cpp`)
   - `PNGdec` by Larry Bank (for Stadia Maps tile decoding)
2. Copy `TinyPlaneTracker/TFT_UserSetup.h` to
   `Arduino/libraries/TFT_eSPI/User_Setup.h` (replaces the library's default).
3. Board settings: **ESP32S3 Dev Module**, Flash Size **4MB**, PSRAM
   **Disabled**, Partition Scheme **Huge APP (3MB No OTA/1MB SPIFFS)**.
4. Open `TinyPlaneTracker/TinyPlaneTracker.ino` and upload.

## First-time setup

On first boot (no saved config), the device starts a WiFi access point
named `TinyPlanes`. Connect to it, then either scan the QR code shown on the
display or open `192.168.4.1` in a browser, to enter your home WiFi
credentials, location (ZIP lookup or lat/lon), refresh rate, display range,
auto-zoom bounds, and background mode. Saving restarts the device straight
into normal tracking.

Two other ways into the config portal, without re-flashing — see
[buttons.md](buttons.md) for full detail:
- Hold the left button (GPIO5) for 2+ seconds while the tracker is running.
- Press the right button (GPIO13) at any point while the device is trying to
  connect to WiFi at boot, instead of waiting out the retry timeout.

The QR code is pre-generated at build time (it just encodes the fixed
`http://192.168.4.1` config-portal address) rather than computed on-device —
no QR-encoding library dependency. If `AP_IP` in `config.h` is ever changed,
regenerate the bitmap (`QR_MODULES`/`QR_SIZE` in `setup_server.cpp`) with the
standard `qrcode` npm package, e.g.:
```
npx qrcode -t utf8 --ecLevel L "http://<new AP_IP>"
```
then re-pack the resulting module grid into bits (row-major, MSB first).

## License

Apache License 2.0 — see [LICENSE](LICENSE).
