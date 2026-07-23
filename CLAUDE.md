# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Arduino sketch for an ESP32-S3 + Waveshare 1.28" round GC9A01 display (240×240) that tracks nearby aircraft via the [ADSB.fi](https://adsb.fi) API and renders them as directional triangles + callsign/model labels on the round display.

## Build & Flash

This is an Arduino IDE project — there is no `make`, `cmake`, or automated test suite. Compile and upload through Arduino IDE with the ESP32 Arduino core installed.

1. Install these libraries via Library Manager:
   - `TFT_eSPI` by Bodmer
   - `ArduinoJson` v6 (if v7 is installed, rename `DynamicJsonDocument` → `JsonDocument` in `flights.cpp`)
   - `PNGdec` by Larry Bank (Stadia Maps tile decoding)
2. Copy `TinyPlaneTracker/TFT_UserSetup.h` → `Arduino/libraries/TFT_eSPI/User_Setup.h` (replaces the library's default) before compiling.
3. Board settings: **ESP32S3 Dev Module**, Flash Size **4MB** (actual chip size — larger settings fail to boot), PSRAM **Disabled** (board has none), Partition Scheme **Huge APP (3MB No OTA/1MB SPIFFS)**.
4. Open `TinyPlaneTracker/TinyPlaneTracker.ino` and upload.

There's no way to test changes without physical hardware — verify by flashing and observing serial output (115200 baud) and the display.

## Architecture

The sketch has two runtime modes, selected in `setup()` based on whether NVS holds a saved config:

- **Setup mode** (`setup_server.cpp`) — ESP32 runs as a WiFi AP (`TinyPlanes`, open network). Serves a single-page HTML config form at `192.168.4.1`, and shows a pre-generated QR code on the display encoding that URL so a phone can scan straight to the page instead of typing it. On save, writes `AppConfig` to NVS and restarts. Entered three ways: no saved config (`startSetupServer()`, blocks forever); GPIO5 held 2s while the tracker is running (`onDecHold()` → `enterSetupModeLive()`, live, no reboot); GPIO13 pressed at any point during the boot-time WiFi connect attempt (same `enterSetupModeLive()`, aborting the ~20s retry loop early). There is **no** boot-time countdown/timeout window — config-exists boots go straight from `loadConfig()` into `connectWiFi()`, by explicit design (a prior countdown-on-every-boot behavior was deliberately removed).
- **Tracker mode** (`TinyPlaneTracker.ino` + `flights.cpp` + `display.cpp`) — connects to home WiFi as a station, fetches nearby aircraft from ADSB.fi, merges them into a persistent display list, and redraws the round display. Runs continuously in `loop()` — no deep sleep.

### Key files

| File | Role |
|---|---|
| `config.h` | All compile-time constants: pins, NVS namespace, AP SSID/IP, button timing, range/refresh bounds |
| `storage.h/cpp` | NVS read/write via `Preferences.h` — single `AppConfig` struct holds every user-configurable setting |
| `setup_server.h/cpp` | AP + `WebServer`, HTML/JS config form served from `PROGMEM`, pre-generated QR bitmap + blit helper |
| `flights.h/cpp` | ADSB.fi fetch via `HTTPClient`/`ArduinoJson`, `PlaneState` struct (callsign, ICAO type code, lat/lon/track) |
| `type_names.h` | ICAO type code → human-readable aircraft name lookup table (flash-resident `const` data, not RAM) |
| `display.h/cpp` | TFT_eSPI rendering: plane triangles + labels, dark-ring or Stadia-map background, status screens |
| `TinyPlaneTracker.ino` | Globals, WiFi connect, button state machine, `setup()`/`loop()` |
| `imu.h/cpp` | QMI8658 double-tap detection code — exists but is **not called** from `setup()`/`loop()`; dead code as of this writing |
| `TFT_UserSetup.h` | TFT_eSPI pin config for this board — must be copied to the library folder before building |
| `buttons.md` | Source-of-truth reference for the physical button behavior (see below) — read this, don't infer button behavior from code comments alone |

### Two physical buttons (see `buttons.md`)

`buttons.md` at the repo root is the authoritative, living reference for Left (GPIO5) / Right (GPIO13) button behavior — tap, hold, and combined-hold actions, plus which functions/constants implement each. Update it in the same change whenever button logic changes; read it before modifying button behavior rather than re-deriving it from the code. In short: taps adjust display range, holds trigger restart/config-mode, and holding both together toggles auto-zoom.

### Data flow / rendering

`fetchWithAutoZoom()` calls `fetchFlights()` (ADSB.fi over HTTPS, filtered via `ArduinoJson`'s `DeserializationOption::Filter` to keep only needed fields) and, if `autoZoom` is enabled, adjusts `cfg.rangeMiles` ±2mi per fetch to keep the aircraft count within `minPlanesInView`/`maxPlanesInView`. `mergePlanes()` then reconciles the fresh fetch against the persistent `planes[]` array: on first fill it spreads initial aircraft evenly across NE/SE/SW/NW quadrants (so airport-dominated traffic doesn't monopolize the display); afterward it matches by callsign, drops aircraft that left range, and backfills vacated slots with the fresh aircraft farthest from home (prioritizing screen edges). `drawPlanes()` does a defensive `initDisplay()` re-init on every call (recovers the display if a prior redraw left the GC9A01 in a bad state) before drawing the background and per-plane labels.

### Critical constraints

- **WiFi must initialize before TFT_eSPI** — DMA channel conflict on ESP32-S3 if reversed. This is why `connectWiFi()` runs before `initDisplay()` in `setup()`, and why `showSetupScreen()` brings up `WiFi.softAP()` before touching the TFT.
- **No PSRAM** — use `malloc()` only; the 112KB Stadia-map background buffer (`allocBackgroundBuffer()`) is the largest single allocation and has a dark-mode fallback if it fails. Static/flash-resident data (like `type_names.h`'s lookup table) is preferred over anything RAM-resident when the content doesn't change at runtime.
- **ArduinoJson v6 vs v7** — the codebase uses v6's `DynamicJsonDocument`/`StaticJsonDocument`; v7 renames these to `JsonDocument`.
- **Arduino auto-prototype hoisting** — the Arduino build tool auto-generates function prototypes and inserts them near the top of the `.ino` file, above any type defined later in the file. Never use a custom `struct`/`class` as a function parameter type in `TinyPlaneTracker.ino` — it will fail to compile ("not declared in this scope") because the hoisted prototype appears before the type definition. Stick to primitive/Arduino-core types (`bool`, `uint8_t`, `unsigned long`, references to them) in `.ino` function signatures; put custom types in a header/source pair instead if needed.
- **ADSB.fi only provides a short ICAO type code** (field `"t"`, e.g. "B738"), not a full aircraft name — full names come from the `type_names.h` lookup table, which only covers common commercial/regional types; codes not in the table fall back to displaying the raw code.
