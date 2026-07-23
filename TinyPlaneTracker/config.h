#pragma once

// ─── WiFi Access Point (setup mode) ───────────────────────────────────────────
#define AP_SSID         "TinyPlanes"
#define AP_PASSWORD     ""              // empty string = open network
#define AP_IP           "192.168.4.1"

// ─── Display ──────────────────────────────────────────────────────────────────
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define LCD_BL_PIN      40             // backlight GPIO
#define LCD_RST_PIN     12             // display reset — must be held HIGH during deep sleep
#define LCD_CS_PIN      9              // SPI chip-select — must be held HIGH during deep sleep

// ─── Map tile settings ────────────────────────────────────────────────────────
#define TILE_SIZE_PX    256            // Stadia tile pixel dimension
#define TILE_GRID       2              // fetch a 2×2 grid
#define STITCH_SIZE     (TILE_SIZE_PX * TILE_GRID)  // 512
// Zoom level is computed dynamically by rangeToZoom() in display.cpp

// ─── Stadia Maps base layer ───────────────────────────────────────────────────
// API key is stored in NVS (stadiaKey field in AppConfig)
#define STADIA_STYLE    "stamen_toner_lite"

// ─── Background mode ──────────────────────────────────────────────────────────
#define BG_DARK         0
#define BG_MAP          1

// ─── Aircraft info display ─────────────────────────────────────────────────────
#define MODEL_FMT_CODE  0   // e.g. "B738"
#define MODEL_FMT_NAME  1   // e.g. "Boeing 737-800" (via type_names.h lookup)

// ─── Flight data ──────────────────────────────────────────────────────────────
#define MAX_PLANES      60   // persistent display list — never shrinks mid-session
#define FETCH_BUF       80   // intermediate fetch buffer per API call

// ─── Refresh bounds (seconds) ─────────────────────────────────────────────────
#define REFRESH_MIN     5
#define REFRESH_MAX     120
#define REFRESH_DEFAULT 15

// ─── Range defaults (miles) ───────────────────────────────────────────────────
#define RANGE_DEFAULT       30
#define RANGE_MIN_DEFAULT    2
#define RANGE_MAX_DEFAULT   60

// ─── Auto-zoom plane count defaults ──────────────────────────────────────────
#define PLANES_MIN_DEFAULT   1
#define PLANES_MAX_DEFAULT  20
#define AUTOZOOM_DEFAULT     1   // 1 = enabled

// ─── Manual range buttons ─────────────────────────────────────────────────────
#define RANGE_BUTTON_INC_PIN 13  // momentary switch to GND, internal pull-up — range +5mi / hold 2s -> restart
#define RANGE_BUTTON_DEC_PIN  5  // momentary switch to GND, internal pull-up — range -5mi / hold 2s -> config mode
#define RANGE_STEP_MILES      5
#define DEBOUNCE_MS          50
#define BUTTON_HOLD_MS      2000  // held this long while running -> long-hold action instead of tap

// ─── IMU (QMI8658 on Waveshare ESP32-S3-LCD-1.28) ────────────────────────────
#define IMU_SDA_PIN     6
#define IMU_SCL_PIN     7
#define IMU_ADDR        0x6B

// ─── NVS namespace ────────────────────────────────────────────────────────────
#define NVS_NAMESPACE   "tinyplanes"

// ─── HTTP timeouts (ms) ───────────────────────────────────────────────────────
#define HTTP_TIMEOUT_MS 30000
