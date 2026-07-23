# Button Functions

Physical button reference for TinyPlaneTracker. This is the source of truth for
button behavior — keep it in sync whenever tap/hold/combo logic changes in
`TinyPlaneTracker/TinyPlaneTracker.ino` or `TinyPlaneTracker/config.h`.

| Button | GPIO | Tap (release before hold threshold) | Hold (held past threshold) |
|--------|------|--------------------------------------|------------------------------|
| Left   | 5    | Decrease range by 5mi (clamped to `minRangeMiles`; disables auto-zoom) | Enter config mode — live, no reboot (disconnects STA WiFi, starts the AP config portal) |
| Right  | 13   | Increase range by 5mi (clamped to `maxRangeMiles`; disables auto-zoom) | Restart the device (`ESP.restart()`) |

## Combined hold (Left + Right together)

Holding both buttons down at the same time for the hold threshold toggles
auto-zoom on/off (saved to config, display redraws immediately). The timer
starts from whichever button was pressed *second* — i.e. it requires that
full duration with both buttons simultaneously down.

Combined hold **suppresses** each button's own tap and hold action for that
press — holding both together will never also restart the device or enter
config mode. Tapping either button afterward still force-disables auto-zoom
as usual; the combined hold is the only way to turn it back on.

## Boot-time only: Right button during WiFi connect

This is a separate, boot-phase-only behavior — distinct from the Right
button's normal runtime tap/hold above, which only applies once the tracker
is running (`loop()`). While `connectWiFi()` is retrying the home WiFi
connection at boot (before the display/tracker start), pressing the Right
button (GPIO13) at any point — a single press, no hold required — aborts the
connection attempt immediately and enters config mode live (same
`enterSetupModeLive()` used by the Left-hold above), instead of waiting out
the ~20s retry window and rebooting into "WiFi failed".

## Display indicator

The range label at the bottom of the screen shows `"N mi (a)"` in blue when
auto-zoom is on, or `"N mi"` in dim gray when off.

## Implementation reference

- Pin/timing constants — `TinyPlaneTracker/config.h`:
  - `RANGE_BUTTON_DEC_PIN` (5, Left), `RANGE_BUTTON_INC_PIN` (13, Right)
  - `RANGE_STEP_MILES` (5) — tap step size
  - `BUTTON_HOLD_MS` (2000) — hold threshold, applies to solo holds and the combined hold
  - `DEBOUNCE_MS` (50)
- Logic — `TinyPlaneTracker/TinyPlaneTracker.ino`:
  - `pollRangeButtons()` — top-level poll, called every `loop()` iteration; detects the combined hold and dispatches to solo tap/hold otherwise
  - `debounceButton()` — per-button debounce, tracks press start time and a release edge
  - `onIncTap()` / `onDecTap()` — tap actions (call `adjustRange()`)
  - `onIncHold()` / `onDecHold()` — solo hold actions
  - `onComboHold()` — combined hold action (toggles `cfg.autoZoom`)
  - `enterSetupModeLive()` — shared helper (disconnect STA WiFi, start the AP portal live, never returns); used by `onDecHold()` and the boot-time Right-button check inside `connectWiFi()`
- Display — `TinyPlaneTracker/display.cpp`:
  - `drawPlanes()` — renders the range label with the `(a)` suffix/color
- Setup portal screen — `TinyPlaneTracker/setup_server.cpp`:
  - `showSetupScreen()` — shown on every config-mode entry point (no saved config, Left-hold, Right-button-during-connect); includes a pre-generated QR code (`QR_MODULES`/`drawQRCode()`) encoding `http://` + `AP_IP`, so scanning it after joining the `AP_SSID` WiFi network jumps straight to the config page
