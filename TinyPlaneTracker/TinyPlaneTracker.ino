#include <TFT_eSPI.h>
#include <WiFi.h>
#include <math.h>
#include "esp_system.h"
#include "config.h"
#include "storage.h"
#include "setup_server.h"
#include "display.h"
#include "flights.h"
#include "imu.h"

// Diagnostic: identify why the last reset happened (brownout, panic, watchdog, etc.)
static const char* resetReasonStr(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT_PIN";
        case ESP_RST_SW:        return "SW_RESET";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP_WAKE";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}
// Global TFT instance — extern'd by display.cpp and setup_server.cpp
TFT_eSPI tft;

static AppConfig   cfg;
static PlaneState  planes[MAX_PLANES];  // persistent: grows only, never shrinks
static int         planeCount  = 0;
static PlaneState  fetchBuf[FETCH_BUF]; // scratch buffer for each API call
static uint16_t*   bgBuffer    = nullptr;
static unsigned long lastFetch = 0;

// ─── WiFi connect ─────────────────────────────────────────────────────────────

static bool connectWiFi(const AppConfig& c) {
    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.begin(c.wifiSSID, c.wifiPass);
    Serial.printf("[main] connecting to SSID: '%s'\n", c.wifiSSID);
    Serial.print("[main] WiFi connecting");
    for (int i = 0; i < 40; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf(" OK  IP %s\n", WiFi.localIP().toString().c_str());
            delay(1000);
            return true;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println(" FAILED");
    return false;
}

// ─── Spatial spread helpers ───────────────────────────────────────────────────

// Return which of the 8 cardinal/intercardinal sectors (N=0, NE=1, … NW=7)
// the given lat/lon falls in relative to the configured home position.
static int planeSector(float lat, float lon) {
    float dy = lat - cfg.latitude;
    float dx = (lon - cfg.longitude) * cosf(cfg.latitude * (float)M_PI / 180.0f);
    float bear = fmodf(atan2f(dx, dy) * (180.0f / (float)M_PI) + 360.0f, 360.0f);
    return ((int)(bear / 45.0f)) & 7;
}

static void mergePlanes(const PlaneState* fresh, int freshCount) {
    bool freshUsed[FETCH_BUF] = {};

    if (planeCount == 0) {
        // ── Initial fill: cycle through NE/SE/SW/NW quadrants, pick one at
        //    random from each before moving on. Ensures geographic spread even
        //    when airport traffic dominates a particular direction.
        int qIdx[4][FETCH_BUF];
        int qCnt[4] = {};
        for (int f = 0; f < freshCount; f++) {
            int q;
            if      (fresh[f].lat >= cfg.latitude && fresh[f].lon >= cfg.longitude) q = 0; // NE
            else if (fresh[f].lat <  cfg.latitude && fresh[f].lon >= cfg.longitude) q = 1; // SE
            else if (fresh[f].lat <  cfg.latitude && fresh[f].lon <  cfg.longitude) q = 2; // SW
            else                                                                    q = 3; // NW
            qIdx[q][qCnt[q]++] = f;
        }

        int q = 0;
        while (planeCount < MAX_PLANES) {
            if (qCnt[0] + qCnt[1] + qCnt[2] + qCnt[3] == 0) break;
            while (qCnt[q] == 0) q = (q + 1) & 3;   // skip empty quadrants
            int pick = random(qCnt[q]);
            int f    = qIdx[q][pick];
            planes[planeCount++] = fresh[f];
            freshUsed[f] = true;
            qIdx[q][pick] = qIdx[q][--qCnt[q]];      // remove from pool
            q = (q + 1) & 3;
        }

    } else {
        // ── Ongoing: update positions, remove dropped-off planes, refill ──

        // Phase 1: update known planes; compact out any that left the area
        int i = 0;
        while (i < planeCount) {
            int found = -1;
            for (int f = 0; f < freshCount; f++) {
                if (strcmp(planes[i].callsign, fresh[f].callsign) == 0) {
                    found = f; break;
                }
            }
            if (found >= 0) {
                planes[i] = fresh[found];
                freshUsed[found] = true;
                i++;
            } else {
                // Dropped off — fill slot by swapping with the last entry
                planes[i] = planes[--planeCount];
                // Don't advance i; re-check the swapped-in element
            }
        }

        // Phase 2: fill vacated/new slots with the unused fresh plane that is
        //          closest to the edge of the display (farthest from home).
        while (planeCount < MAX_PLANES) {
            int   bestF  = -1;
            float bestD2 = -1.0f;
            for (int f = 0; f < freshCount; f++) {
                if (freshUsed[f]) continue;
                float dx = (fresh[f].lon - cfg.longitude)
                           * cosf(cfg.latitude * (float)M_PI / 180.0f);
                float dy = fresh[f].lat - cfg.latitude;
                float d2 = dx * dx + dy * dy;
                if (d2 > bestD2) { bestD2 = d2; bestF = f; }
            }
            if (bestF < 0) break;
            planes[planeCount++] = fresh[bestF];
            freshUsed[bestF] = true;
        }
    }

    // Debug: sector distribution of what was fetched vs what's now displayed
    int fs[8] = {}, ds[8] = {};
    for (int f = 0; f < freshCount; f++) fs[planeSector(fresh[f].lat, fresh[f].lon)]++;
    for (int d = 0; d < planeCount;  d++) ds[planeSector(planes[d].lat, planes[d].lon)]++;
    Serial.printf("[main] fresh  N=%d NE=%d E=%d SE=%d S=%d SW=%d W=%d NW=%d\n",
                  fs[0],fs[1],fs[2],fs[3],fs[4],fs[5],fs[6],fs[7]);
    Serial.printf("[main] disp   N=%d NE=%d E=%d SE=%d S=%d SW=%d W=%d NW=%d  total=%d\n",
                  ds[0],ds[1],ds[2],ds[3],ds[4],ds[5],ds[6],ds[7], planeCount);
}

// ─── setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.printf("[main] last reset reason: %s\n", resetReasonStr(esp_reset_reason()));
    pinMode(RANGE_BUTTON_INC_PIN, INPUT_PULLUP);
    pinMode(RANGE_BUTTON_DEC_PIN, INPUT_PULLUP);

    bool hasConfig = loadConfig(cfg);
    if (hasConfig)
        Serial.printf("[main] home: lat=%.4f lon=%.4f range=%dmi\n",
                      cfg.latitude, cfg.longitude, cfg.rangeMiles);

    if (!hasConfig) {
        startSetupServer();  // no config: setup forever, never returns
    }

    // Config exists: start immediately. Config mode is reached at runtime
    // instead, via a GPIO5 long-hold (see onDecHold()).

    // Connect WiFi before initDisplay() — DMA constraint.
    if (!connectWiFi(cfg)) {
        initDisplay();
        showStatus("WiFi failed");
        delay(5000);
        ESP.restart();
    }

    initDisplay();

    if (cfg.bgMode == BG_MAP) {
        bgBuffer = allocBackgroundBuffer();
        if (bgBuffer) {
            showStatus("Loading map...");
            fetchMapBackground(cfg, bgBuffer);
        }
    }

    showStatus("Fetching...");
    bool rangeChanged = false;
    int fc = fetchWithAutoZoom(rangeChanged);
    if (rangeChanged) applyRangeChange();
    mergePlanes(fetchBuf, fc);
    drawPlanes(cfg, planes, planeCount, bgBuffer);
    lastFetch = millis();
}

// ─── helpers ─────────────────────────────────────────────────────────────────

// Fetch flights, auto-adjusting range if autoZoom is enabled.
// Sets rangeChanged=true if rangeMiles was modified. Safety cap: 30 iterations.
static int fetchWithAutoZoom(bool& rangeChanged) {
    rangeChanged = false;

    if (!cfg.autoZoom) {
        return fetchFlights(cfg, fetchBuf, FETCH_BUF);
    }

    int origRange = cfg.rangeMiles;

    for (int attempt = 0; attempt < 30; attempt++) {
        int fc = fetchFlights(cfg, fetchBuf, FETCH_BUF);

        if (fc < cfg.minPlanesInView && cfg.rangeMiles < cfg.maxRangeMiles) {
            cfg.rangeMiles = min(cfg.rangeMiles + 2, cfg.maxRangeMiles);
            Serial.printf("[main] %d planes < min(%d) → range %d mi\n",
                          fc, cfg.minPlanesInView, cfg.rangeMiles);
            continue;
        }
        if (fc > cfg.maxPlanesInView && cfg.rangeMiles > cfg.minRangeMiles) {
            cfg.rangeMiles = max(cfg.rangeMiles - 2, cfg.minRangeMiles);
            Serial.printf("[main] %d planes > max(%d) → range %d mi\n",
                          fc, cfg.maxPlanesInView, cfg.rangeMiles);
            continue;
        }

        rangeChanged = (cfg.rangeMiles != origRange);
        return fc;
    }

    rangeChanged = (cfg.rangeMiles != origRange);
    return fetchFlights(cfg, fetchBuf, FETCH_BUF);
}

// Apply a range change: save config, reload map background, reset plane list.
static void applyRangeChange() {
    saveConfig(cfg);
    char msg[20];
    snprintf(msg, sizeof(msg), "Range: %d mi", cfg.rangeMiles);
    showStatus(msg);
    if (cfg.bgMode == BG_MAP && bgBuffer) {
        showStatus("Loading map...");
        fetchMapBackground(cfg, bgBuffer);
    }
    planeCount = 0;
}

// Manual range step from the GPIO13 (+) / GPIO5 (-) button taps. Disables
// autoZoom so the automatic +/-2mi adjustment on the next periodic fetch
// doesn't fight the user's manual choice. Clamped to [minRangeMiles, maxRangeMiles].
static void adjustRange(int deltaMiles) {
    cfg.autoZoom   = false;
    cfg.rangeMiles = constrain(cfg.rangeMiles + deltaMiles, cfg.minRangeMiles, cfg.maxRangeMiles);

    Serial.printf("[main] button: range -> %d mi (autoZoom off)\n", cfg.rangeMiles);
    applyRangeChange();

    showStatus("Fetching...");
    bool unused = false;
    int fc = fetchWithAutoZoom(unused);
    mergePlanes(fetchBuf, fc);
    drawPlanes(cfg, planes, planeCount, bgBuffer);
    lastFetch = millis();
}

static void onIncTap()  { adjustRange(RANGE_STEP_MILES); }
static void onDecTap()  { adjustRange(-RANGE_STEP_MILES); }

// GPIO13 held BUTTON_HOLD_MS -> restart the device.
static void onIncHold() {
    Serial.println("[main] GPIO13 held -> restarting");
    showStatus("Restarting...");
    delay(500);
    ESP.restart();
}

// GPIO5 held BUTTON_HOLD_MS -> drop the running tracker into the AP config
// portal without rebooting. Mirrors connectWiFi()'s disconnect pattern for a
// clean STA->AP transition; startSetupServer() never returns.
static void onDecHold() {
    Serial.println("[main] GPIO5 held -> entering config mode");
    WiFi.disconnect(true);
    delay(200);
    startSetupServer();
}

// Both buttons held together BUTTON_HOLD_MS -> toggle auto-zoom. Persists
// like any other cfg change and redraws immediately (no refetch needed —
// only the range label's "(a)" suffix/color changes).
static void onComboHold() {
    cfg.autoZoom = !cfg.autoZoom;
    Serial.printf("[main] combo hold -> autoZoom %s\n", cfg.autoZoom ? "ON" : "OFF");
    saveConfig(cfg);
    drawPlanes(cfg, planes, planeCount, bgBuffer);
}

// Debounce + tap-vs-hold state for the two momentary switches (active LOW,
// INPUT_PULLUP). Plain primitive statics — a custom struct type here would
// break under Arduino's auto-generated function prototypes, which get
// hoisted above any type defined later in the .ino.
static bool          incLastReading = HIGH, incStableState = HIGH, incHoldFired = false, incComboTouched = false;
static bool          decLastReading = HIGH, decStableState = HIGH, decHoldFired = false, decComboTouched = false;
static unsigned long incLastEdgeTime = 0, decLastEdgeTime = 0;
static unsigned long incPressStart   = 0, decPressStart   = 0;
static bool          comboHoldFired  = false;

// Debounce one button; reports a release edge via releaseEdge so the caller
// can decide whether to fire its tap action (skipped for combo presses).
static void debounceButton(uint8_t pin, bool& lastReading, bool& stableState,
                            unsigned long& lastEdgeTime, unsigned long& pressStart,
                            bool& holdFired, bool& comboTouched, bool& releaseEdge) {
    releaseEdge = false;
    bool reading = digitalRead(pin);
    if (reading != lastReading) {
        lastEdgeTime = millis();
        lastReading  = reading;
    }
    if ((millis() - lastEdgeTime) > DEBOUNCE_MS && reading != stableState) {
        stableState = reading;
        if (stableState == LOW) {
            pressStart   = millis();
            holdFired    = false;
            comboTouched = false;
        } else {
            releaseEdge = true;
        }
    }
}

// Call every loop() iteration. Both buttons held together BUTTON_HOLD_MS ->
// onComboHold() fires once, suppressing each button's own tap/hold for that
// press. Otherwise each button behaves independently: tap on release (< 2s),
// hold action once at the 2s mark (mutually exclusive with its own tap).
static void pollRangeButtons() {
    bool incReleased, decReleased;
    debounceButton(RANGE_BUTTON_INC_PIN, incLastReading, incStableState, incLastEdgeTime, incPressStart, incHoldFired, incComboTouched, incReleased);
    debounceButton(RANGE_BUTTON_DEC_PIN, decLastReading, decStableState, decLastEdgeTime, decPressStart, decHoldFired, decComboTouched, decReleased);

    bool bothHeld = (incStableState == LOW && decStableState == LOW);

    if (bothHeld) {
        incComboTouched = true;
        decComboTouched = true;
        unsigned long comboStart = max(incPressStart, decPressStart);
        if (!comboHoldFired && (millis() - comboStart) >= BUTTON_HOLD_MS) {
            comboHoldFired = true;
            incHoldFired   = true;  // suppress each button's own hold action
            decHoldFired   = true;
            onComboHold();
        }
        return;
    }
    comboHoldFired = false;

    if (incReleased && !incHoldFired && !incComboTouched) onIncTap();
    if (decReleased && !decHoldFired && !decComboTouched) onDecTap();

    if (incStableState == LOW && !incHoldFired && (millis() - incPressStart) >= BUTTON_HOLD_MS) {
        incHoldFired = true;
        onIncHold();
    }
    if (decStableState == LOW && !decHoldFired && (millis() - decPressStart) >= BUTTON_HOLD_MS) {
        decHoldFired = true;
        onDecHold();
    }
}

// ─── loop ────────────────────────────────────────────────────────────────────

void loop() {
    pollRangeButtons();

    if (millis() - lastFetch >= (unsigned long)cfg.refreshSeconds * 1000UL) {
        bool rangeChanged = false;
        int fc = fetchWithAutoZoom(rangeChanged);
        if (rangeChanged) applyRangeChange();
        mergePlanes(fetchBuf, fc);
        drawPlanes(cfg, planes, planeCount, bgBuffer);
        lastFetch = millis();
    }
}
