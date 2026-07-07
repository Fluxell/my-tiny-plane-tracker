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
named `TinyPlanes`. Connect to it and open `192.168.4.1` in a browser to
enter your home WiFi credentials, location (ZIP lookup or lat/lon),
refresh rate, display range, auto-zoom bounds, and background mode. Saving
restarts the device straight into normal tracking.

To re-enter the config portal later without re-flashing, hold the left
button (GPIO5) for 2+ seconds while the tracker is running — see
[buttons.md](buttons.md).

## License

Apache License 2.0 — see [LICENSE](LICENSE).
