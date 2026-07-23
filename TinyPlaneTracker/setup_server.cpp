#include "setup_server.h"
#include "config.h"
#include "storage.h"
#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

static WebServer server(80);

// ─── Pre-generated QR code ─────────────────────────────────────────────────────
// Encodes "http://" AP_IP (config.h) — regenerate only if AP_IP changes.
// Generated once via the standard `qrcode` npm package (ISO/IEC 18004,
// version 1, ECC_LOW) and round-trip verified with an independent decoder
// (jsQR). Packed 1 bit/module, row-major, MSB first — see qrModuleAt().
static const uint8_t QR_SIZE = 21;
static const uint8_t QR_MODULES[] = {
    0xfe, 0x4b, 0xfc, 0x16, 0x90, 0x6e, 0x92, 0xbb, 0x75, 0x85, 0xdb, 0xa3,
    0xae, 0xc1, 0x79, 0x07, 0xfa, 0xaf, 0xe0, 0x07, 0x00, 0xfb, 0xd5, 0x55,
    0x40, 0x47, 0xf8, 0xd4, 0xac, 0x1b, 0x81, 0xc7, 0xbb, 0x04, 0x80, 0x72,
    0xf7, 0xfb, 0x6a, 0xd0, 0x4b, 0x9c, 0xba, 0x8c, 0x55, 0xd4, 0xf5, 0x2e,
    0xa1, 0x89, 0x05, 0x36, 0x4f, 0xee, 0xf1, 0x00,
};

static bool qrModuleAt(uint8_t x, uint8_t y) {
    uint16_t bit = (uint16_t)y * QR_SIZE + x;
    return (QR_MODULES[bit / 8] >> (7 - (bit % 8))) & 1;
}

// Blits the pre-generated QR code centered at (centerX, centerY), moduleScale
// pixels per module. White quiet-zone border is drawn first; background
// already covers "off" modules so only dark ones need drawing.
static void drawQRCode(int centerX, int centerY, int moduleScale) {
    int qrPixelSize = QR_SIZE * moduleScale;
    int startX = centerX - qrPixelSize / 2;
    int startY = centerY - qrPixelSize / 2;

    tft.fillRect(startX - moduleScale, startY - moduleScale,
                 qrPixelSize + moduleScale * 2, qrPixelSize + moduleScale * 2, TFT_WHITE);

    for (uint8_t y = 0; y < QR_SIZE; y++)
        for (uint8_t x = 0; x < QR_SIZE; x++)
            if (qrModuleAt(x, y))
                tft.fillRect(startX + x * moduleScale, startY + y * moduleScale,
                             moduleScale, moduleScale, TFT_BLACK);
}

// ─── HTML pages stored in flash ───────────────────────────────────────────────

static const char SETUP_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TinyPlanes Setup</title>
<style>
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    background: #0f172a; color: #e2e8f0;
    min-height: 100vh; display: flex; align-items: center; justify-content: center;
    padding: 16px;
  }
  .card {
    background: #1e293b; border-radius: 12px; padding: 28px;
    width: 100%; max-width: 420px; box-shadow: 0 4px 24px rgba(0,0,0,0.4);
  }
  h1 { font-size: 1.4rem; color: #38bdf8; margin-bottom: 4px; }
  .sub { font-size: 0.8rem; color: #64748b; margin-bottom: 24px; }
  section { margin-bottom: 20px; }
  h2 { font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.08em;
       color: #94a3b8; margin-bottom: 10px; }
  label { display: block; font-size: 0.85rem; color: #cbd5e1; margin-bottom: 4px; }
  input, select {
    width: 100%; padding: 9px 12px; background: #0f172a; border: 1px solid #334155;
    border-radius: 6px; color: #e2e8f0; font-size: 0.9rem; outline: none;
    transition: border-color 0.15s;
  }
  input:focus, select:focus { border-color: #38bdf8; }
  .field { margin-bottom: 12px; }
  .hint { font-size: 0.75rem; color: #64748b; margin-top: 3px; }
  .zip-row { display: flex; gap: 8px; align-items: flex-end; }
  .zip-row input { flex: 1; }
  .zip-btn {
    padding: 9px 14px; background: #1e40af; border: none; border-radius: 6px;
    color: #fff; cursor: pointer; font-size: 0.85rem; white-space: nowrap;
    transition: background 0.15s;
  }
  .zip-btn:hover { background: #2563eb; }
  #zipMsg { font-size: 0.78rem; margin-top: 4px; min-height: 18px; }
  .ok { color: #4ade80; }
  .err { color: #f87171; }
  .coords { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
  .radio-group { display: flex; gap: 20px; margin-bottom: 4px; }
  .radio-group label { display: flex; align-items: center; gap: 6px; cursor: pointer; }
  .radio-group input[type=radio] { width: auto; }
  .check-label { display: flex; align-items: center; gap: 6px; cursor: pointer; margin-bottom: 4px; }
  .check-label input[type=checkbox] { width: auto; }
  .submit {
    width: 100%; padding: 11px; background: #0284c7; border: none; border-radius: 8px;
    color: #fff; font-size: 1rem; font-weight: 600; cursor: pointer;
    margin-top: 8px; transition: background 0.15s;
  }
  .submit:hover { background: #0369a1; }
  .submit:disabled { background: #334155; cursor: not-allowed; }
  hr { border: none; border-top: 1px solid #1e3a5f; margin: 20px 0; }
</style>
</head>
<body>
<div class="card">
  <h1>&#x2708;&#xFE0F; TinyPlanes</h1>
  <p class="sub">Configure your device once — it will remember these settings.</p>

  <section>
    <h2>Home WiFi</h2>
    <div class="field">
      <label for="ssid">Network name (SSID)</label>
      <input type="text" id="ssid" autocomplete="off" placeholder="Your home WiFi">
    </div>
    <div class="field">
      <label for="pass">Password</label>
      <input type="password" id="pass" autocomplete="current-password" placeholder="WiFi password">
    </div>
  </section>

  <hr>

  <section>
    <h2>Location</h2>
    <div class="field">
      <label>US ZIP code</label>
      <div class="zip-row">
        <input type="text" id="zip" inputmode="numeric" maxlength="5" placeholder="e.g. 90210">
        <button class="zip-btn" onclick="lookupZip()">Look up</button>
      </div>
      <div id="zipMsg"></div>
    </div>
    <div class="coords">
      <div class="field">
        <label for="lat">Latitude</label>
        <input type="number" id="lat" step="0.0001" placeholder="34.0195">
      </div>
      <div class="field">
        <label for="lon">Longitude</label>
        <input type="number" id="lon" step="0.0001" placeholder="-118.4912">
      </div>
    </div>
    <p class="hint">Enter coordinates directly, or use the ZIP lookup above (requires internet on your phone/computer).</p>
  </section>

  <hr>

  <section>
    <h2>Refresh Rate</h2>
    <div class="field">
      <label for="refresh_s">Refresh every (seconds)</label>
      <input type="number" id="refresh_s" min="5" max="120" value="15">
    </div>
  </section>

  <hr>

  <section>
    <h2>Range</h2>
    <div class="field">
      <label for="range_mi">Starting display radius (miles)</label>
      <input type="number" id="range_mi" min="2" max="60" step="2" value="30">
      <p class="hint">Auto-zoom adjusts this value each refresh to keep traffic density in range.</p>
    </div>
  </section>

  <hr>

  <section>
    <h2>Auto-Zoom</h2>
    <div class="field">
      <div class="radio-group">
        <label><input type="radio" name="auto_zoom" value="1" checked onchange="toggleAutoZoom()"> Enabled</label>
        <label><input type="radio" name="auto_zoom" value="0" onchange="toggleAutoZoom()"> Disabled</label>
      </div>
      <p class="hint">When enabled, the range label on the display turns blue.</p>
    </div>
    <div id="autoZoomSettings">
      <div class="coords">
        <div class="field">
          <label for="min_range_mi">Min range (miles)</label>
          <input type="number" id="min_range_mi" min="2" max="58" step="2" value="2">
        </div>
        <div class="field">
          <label for="max_range_mi">Max range (miles)</label>
          <input type="number" id="max_range_mi" min="4" max="60" step="2" value="60">
        </div>
      </div>
      <div class="coords">
        <div class="field">
          <label for="min_planes">Min planes in view</label>
          <input type="number" id="min_planes" min="0" max="99" value="1">
        </div>
        <div class="field">
          <label for="max_planes">Max planes in view</label>
          <input type="number" id="max_planes" min="1" max="99" value="20">
        </div>
      </div>
      <p class="hint">Range expands when fewer than Min planes are visible; shrinks when more than Max are visible.</p>
    </div>
  </section>

  <hr>

  <section>
    <h2>Background</h2>
    <div class="field">
      <div class="radio-group">
        <label><input type="radio" name="bg_mode" value="0" checked onchange="toggleStadia()"> Dark</label>
        <label><input type="radio" name="bg_mode" value="1" onchange="toggleStadia()"> Map (Stadia)</label>
      </div>
    </div>
    <div id="stadiaDiv" style="display:none">
      <div class="field">
        <label for="stadia_key">Stadia Maps API key</label>
        <input type="text" id="stadia_key" placeholder="Get a free key at client.stadiamaps.com">
        <p class="hint">Free account: 200,000 tile requests/month — well above what this device uses.</p>
      </div>
    </div>
  </section>

  <hr>

  <section>
    <h2>Aircraft Info</h2>
    <div class="field">
      <label class="check-label"><input type="checkbox" id="show_callsign" checked> Show callsign</label>
      <label class="check-label"><input type="checkbox" id="show_model" onchange="toggleModelFormat()"> Show model</label>
    </div>
    <div id="modelFormatDiv" style="display:none">
      <div class="radio-group">
        <label><input type="radio" name="model_format" value="0" checked> Code (e.g. B738)</label>
        <label><input type="radio" name="model_format" value="1"> Full name (e.g. Boeing 737-800)</label>
      </div>
      <p class="hint">Full name uses a built-in lookup table — uncommon types fall back to the code.</p>
    </div>
  </section>

  <button class="submit" onclick="save()">Save &amp; Start Tracker</button>
</div>

<script>
function toggleStadia() {
  const isMap = document.querySelector('[name=bg_mode]:checked').value === '1';
  document.getElementById('stadiaDiv').style.display = isMap ? 'block' : 'none';
}

function toggleAutoZoom() {
  const on = document.querySelector('[name=auto_zoom]:checked').value === '1';
  document.getElementById('autoZoomSettings').style.display = on ? 'block' : 'none';
}

function toggleModelFormat() {
  const on = document.getElementById('show_model').checked;
  document.getElementById('modelFormatDiv').style.display = on ? 'block' : 'none';
}

function lookupZip() {
  const zip = document.getElementById('zip').value.trim();
  const msg = document.getElementById('zipMsg');
  if (!/^\d{5}$/.test(zip)) { msg.className='err'; msg.textContent='Enter a 5-digit ZIP code.'; return; }
  msg.className=''; msg.textContent='Looking up…';
  fetch('https://api.zippopotam.us/us/' + zip)
    .then(r => { if (!r.ok) throw new Error('not found'); return r.json(); })
    .then(d => {
      const p = d.places[0];
      document.getElementById('lat').value = parseFloat(p.latitude).toFixed(4);
      document.getElementById('lon').value = parseFloat(p.longitude).toFixed(4);
      msg.className = 'ok';
      msg.textContent = '✓ ' + p['place name'] + ', ' + p['state abbreviation'];
    })
    .catch(() => { msg.className='err'; msg.textContent='ZIP not found — enter coordinates manually.'; });
}

function save() {
  const ssid = document.getElementById('ssid').value.trim();
  const lat  = parseFloat(document.getElementById('lat').value);
  const lon  = parseFloat(document.getElementById('lon').value);
  if (!ssid)           { alert('Please enter your WiFi network name.'); return; }
  if (isNaN(lat) || isNaN(lon)) { alert('Please enter a valid location.'); return; }
  if (lat < -90  || lat > 90)  { alert('Latitude must be between -90 and 90.'); return; }
  if (lon < -180 || lon > 180) { alert('Longitude must be between -180 and 180.'); return; }

  const bgMode   = document.querySelector('[name=bg_mode]:checked').value;
  const stadiaKey = document.getElementById('stadia_key').value.trim();
  if (bgMode === '1' && !stadiaKey) {
    alert('Please enter your Stadia Maps API key, or select Dark background.');
    return;
  }

  const showCallsign = document.getElementById('show_callsign').checked;
  const showModel    = document.getElementById('show_model').checked;
  if (!showCallsign && !showModel) {
    alert('Please enable at least one of Show callsign or Show model.');
    return;
  }

  const f = document.createElement('form');
  f.method = 'POST'; f.action = '/save';
  const data = {
    ssid,
    pass:         document.getElementById('pass').value,
    lat:          lat.toFixed(6),
    lon:          lon.toFixed(6),
    refresh_s:    document.getElementById('refresh_s').value,
    range_mi:     document.getElementById('range_mi').value,
    bg_mode:      bgMode,
    stadia_key:   stadiaKey,
    auto_zoom:    document.querySelector('[name=auto_zoom]:checked').value,
    min_range_mi: document.getElementById('min_range_mi').value,
    max_range_mi: document.getElementById('max_range_mi').value,
    min_planes:   document.getElementById('min_planes').value,
    max_planes:   document.getElementById('max_planes').value,
    show_callsign: showCallsign ? '1' : '0',
    show_model:    showModel ? '1' : '0',
    model_format:  document.querySelector('[name=model_format]:checked').value
  };
  for (const [k, v] of Object.entries(data)) {
    const i = document.createElement('input');
    i.type = 'hidden'; i.name = k; i.value = v;
    f.appendChild(i);
  }
  document.body.appendChild(f);
  f.submit();
}

async function loadSaved() {
  try {
    const r = await fetch('/config');
    if (!r.ok) return;
    const d = await r.json();
    if (d.ssid)                document.getElementById('ssid').value       = d.ssid;
    if (d.pass)                document.getElementById('pass').value       = d.pass;
    if (d.lat  != null)        document.getElementById('lat').value        = parseFloat(d.lat).toFixed(4);
    if (d.lon  != null)        document.getElementById('lon').value        = parseFloat(d.lon).toFixed(4);
    if (d.refresh_s    != null) document.getElementById('refresh_s').value    = d.refresh_s;
    if (d.range_mi     != null) document.getElementById('range_mi').value     = d.range_mi;
    if (d.auto_zoom    != null) {
      document.querySelectorAll('[name=auto_zoom]').forEach(r => {
        r.checked = (r.value === String(d.auto_zoom));
      });
      toggleAutoZoom();
    }
    if (d.min_range_mi != null) document.getElementById('min_range_mi').value = d.min_range_mi;
    if (d.max_range_mi != null) document.getElementById('max_range_mi').value = d.max_range_mi;
    if (d.min_planes   != null) document.getElementById('min_planes').value   = d.min_planes;
    if (d.max_planes   != null) document.getElementById('max_planes').value   = d.max_planes;
    if (d.bg_mode      != null) {
      document.querySelectorAll('[name=bg_mode]').forEach(r => {
        r.checked = (r.value === String(d.bg_mode));
      });
      toggleStadia();
    }
    if (d.stadia_key)          document.getElementById('stadia_key').value = d.stadia_key;
    if (d.show_callsign != null) document.getElementById('show_callsign').checked = (String(d.show_callsign) === '1');
    if (d.show_model    != null) document.getElementById('show_model').checked    = (String(d.show_model) === '1');
    if (d.model_format   != null) {
      document.querySelectorAll('[name=model_format]').forEach(r => {
        r.checked = (r.value === String(d.model_format));
      });
    }
    toggleModelFormat();
  } catch(e) {}
}
document.addEventListener('DOMContentLoaded', loadSaved);
</script>
</body>
</html>
)HTML";

static const char SAVED_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TinyPlanes — Saved</title>
<style>
  body {
    font-family: -apple-system, sans-serif; background: #0f172a; color: #e2e8f0;
    min-height: 100vh; display: flex; align-items: center; justify-content: center;
    text-align: center; padding: 24px;
  }
  h1 { color: #4ade80; font-size: 1.5rem; margin-bottom: 12px; }
  p  { color: #94a3b8; }
</style>
</head>
<body>
  <div>
    <h1>&#x2713; Saved!</h1>
    <p>TinyPlanes is restarting.<br>It will connect to your WiFi and start tracking flights.</p>
  </div>
</body>
</html>
)HTML";

// ─── Screen helpers ───────────────────────────────────────────────────────────

static void showSavedScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Settings saved!", DISPLAY_WIDTH / 2, 100, 2);
    tft.drawString("Starting...", DISPLAY_WIDTH / 2, 130, 2);
}

// ─── Setup screen ─────────────────────────────────────────────────────────────

static void showSetupScreen() {
    pinMode(LCD_BL_PIN, OUTPUT);
    digitalWrite(LCD_BL_PIN, HIGH);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    const int cx = DISPLAY_WIDTH / 2;

    drawQRCode(cx, 110, 4);  // "http://" AP_IP — scan after joining AP_SSID

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("TinyPlanes", cx, 190, 2);
    tft.setTextColor(tft.color565(148, 163, 184), TFT_BLACK);
    tft.drawString("Connect to: " AP_SSID, cx, 210, 1);
}

// ─── Route handlers ───────────────────────────────────────────────────────────

static String jsonStr(const char* s) {
    String out = "\"";
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') out += '\\';
        out += *s;
    }
    out += "\"";
    return out;
}

static void handleConfig() {
    AppConfig saved = {};
    if (!loadConfig(saved)) {
        server.send(204, "text/plain", "");
        return;
    }
    String json = "{";
    json += "\"ssid\":"      + jsonStr(saved.wifiSSID)       + ",";
    json += "\"pass\":"      + jsonStr(saved.wifiPass)       + ",";
    json += "\"lat\":"       + String(saved.latitude, 6)     + ",";
    json += "\"lon\":"       + String(saved.longitude, 6)    + ",";
    json += "\"refresh_s\":"    + String(saved.refreshSeconds)   + ",";
    json += "\"range_mi\":"     + String(saved.rangeMiles)       + ",";
    json += "\"bg_mode\":"      + String(saved.bgMode)           + ",";
    json += "\"stadia_key\":"   + jsonStr(saved.stadiaKey)        + ",";
    json += "\"auto_zoom\":"    + String(saved.autoZoom ? 1 : 0) + ",";
    json += "\"min_range_mi\":" + String(saved.minRangeMiles)    + ",";
    json += "\"max_range_mi\":" + String(saved.maxRangeMiles)    + ",";
    json += "\"min_planes\":"   + String(saved.minPlanesInView)  + ",";
    json += "\"max_planes\":"   + String(saved.maxPlanesInView)  + ",";
    json += "\"show_callsign\":" + String(saved.showCallsign ? 1 : 0) + ",";
    json += "\"show_model\":"    + String(saved.showModel ? 1 : 0)    + ",";
    json += "\"model_format\":"  + String(saved.modelFormat);
    json += "}";
    server.send(200, "application/json", json);
}

static void handleRoot() {
    server.send_P(200, "text/html", SETUP_HTML);
}

static void handleSave() {
    if (!server.hasArg("ssid") || !server.hasArg("lat") || !server.hasArg("lon")) {
        server.send(400, "text/plain", "Missing fields");
        return;
    }

    AppConfig cfg = {};
    server.arg("ssid").toCharArray(cfg.wifiSSID, sizeof(cfg.wifiSSID));
    server.arg("pass").toCharArray(cfg.wifiPass, sizeof(cfg.wifiPass));
    cfg.latitude       = server.arg("lat").toFloat();
    cfg.longitude      = server.arg("lon").toFloat();
    cfg.refreshSeconds  = constrain(server.arg("refresh_s").toInt(),    REFRESH_MIN, REFRESH_MAX);
    cfg.autoZoom        = server.arg("auto_zoom").toInt() != 0;
    cfg.minRangeMiles   = constrain(server.arg("min_range_mi").toInt(), 2, 58);
    cfg.maxRangeMiles   = constrain(server.arg("max_range_mi").toInt(), 4, 60);
    cfg.minPlanesInView = constrain(server.arg("min_planes").toInt(),   0, 99);
    cfg.maxPlanesInView = constrain(server.arg("max_planes").toInt(),   1, 99);
    cfg.rangeMiles      = constrain(server.arg("range_mi").toInt(),
                                    cfg.minRangeMiles, cfg.maxRangeMiles);
    cfg.bgMode = (uint8_t)constrain(server.arg("bg_mode").toInt(), 0, 1);
    server.arg("stadia_key").toCharArray(cfg.stadiaKey, sizeof(cfg.stadiaKey));
    cfg.showCallsign = server.arg("show_callsign").toInt() != 0;
    cfg.showModel    = server.arg("show_model").toInt() != 0;
    cfg.modelFormat  = (uint8_t)constrain(server.arg("model_format").toInt(), 0, 1);

    saveConfig(cfg);
    server.send_P(200, "text/html", SAVED_HTML);
    showSavedScreen();

    // Briefly pump the server so the HTTP response actually flushes to the
    // browser before rebooting — no countdown, straight to starting.
    unsigned long until = millis() + 500;
    while (millis() < until) {
        server.handleClient();
        delay(10);
    }
    ESP.restart();
}

static void handleNotFound() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

// ─── Public entry points ──────────────────────────────────────────────────────

void startSetupServer() {
    Serial.println("[setup] starting AP"); Serial.flush();
    WiFi.persistent(false);
    bool apOK = (strlen(AP_PASSWORD) > 0)
        ? WiFi.softAP(AP_SSID, AP_PASSWORD)
        : WiFi.softAP(AP_SSID);
    Serial.printf("[setup] softAP: %d  IP: %s\n", apOK ? 1 : 0,
                  WiFi.softAPIP().toString().c_str());
    delay(500);

    showSetupScreen();

    server.on("/",       HTTP_GET,  handleRoot);
    server.on("/config", HTTP_GET,  handleConfig);
    server.on("/save",   HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();

    Serial.println("[setup] entering loop");
    while (true) {
        server.handleClient();
        delay(2);
    }
}
