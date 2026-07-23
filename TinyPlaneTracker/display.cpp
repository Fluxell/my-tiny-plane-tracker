#include "display.h"
#include "config.h"
#include "type_names.h"
#include <TFT_eSPI.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PNGdec.h>
#include <math.h>

extern TFT_eSPI tft;

// ─── Module-level state ───────────────────────────────────────────────────────

static PNG png;
static const size_t TILE_BUF_SIZE = 65536;
static uint8_t s_tileBuf[TILE_BUF_SIZE];  // static to avoid stack overflow

struct RenderCtx {
    int tileOffX, tileOffY;
    int cropX,    cropY;
    int gridOriginTileX;
    int gridOriginTileY;
};
static RenderCtx s_ctx;

static uint16_t* s_bgBuf = nullptr;  // target buffer for map background decode

// Stadia key used during tile fetch (set from AppConfig before fetching)
static char s_stadiaKey[64] = "";

// ─── Mercator tile math (verbatim from TinyWeatherRadar) ─────────────────────

static int lon2tileX(double lon, int z) {
    return (int)floor((lon + 180.0) / 360.0 * (1 << z));
}

static int lat2tileY(double lat, int z) {
    double rad = lat * DEG_TO_RAD;
    return (int)floor((1.0 - log(tan(rad) + 1.0 / cos(rad)) / M_PI) / 2.0 * (1 << z));
}

static int lon2subPixel(double lon, int z) {
    double tx = (lon + 180.0) / 360.0 * (1 << z);
    return (int)((tx - floor(tx)) * TILE_SIZE_PX);
}

static int lat2subPixel(double lat, int z) {
    double rad = lat * DEG_TO_RAD;
    double ty  = (1.0 - log(tan(rad) + 1.0 / cos(rad)) / M_PI) / 2.0 * (1 << z);
    return (int)((ty - floor(ty)) * TILE_SIZE_PX);
}

// ─── PNG callbacks ────────────────────────────────────────────────────────────

// Write decoded map tile row into the background buffer.
static int mapToBufferCallback(PNGDRAW* pDraw) {
    int stitchRow  = s_ctx.tileOffY + pDraw->y;
    int displayRow = stitchRow - s_ctx.cropY;
    if (displayRow < 0 || displayRow >= DISPLAY_HEIGHT) return 1;

    int overlapStart = max(s_ctx.tileOffX,                s_ctx.cropX);
    int overlapEnd   = min(s_ctx.tileOffX + TILE_SIZE_PX, s_ctx.cropX + DISPLAY_WIDTH);
    if (overlapStart >= overlapEnd) return 1;

    int tileCol    = overlapStart - s_ctx.tileOffX;
    int displayCol = overlapStart - s_ctx.cropX;
    int width      = overlapEnd - overlapStart;

    uint16_t line[TILE_SIZE_PX];
    png.getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0x0000);
    memcpy(&s_bgBuf[displayRow * DISPLAY_WIDTH + displayCol],
           &line[tileCol], width * sizeof(uint16_t));
    return 1;
}

// ─── HTTP helper ──────────────────────────────────────────────────────────────

static int httpGetBinary(const String& url) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(HTTP_TIMEOUT_MS);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[display] HTTP %d  %s\n", code, url.c_str());
        http.end();
        return 0;
    }

    auto*         stream     = http.getStreamPtr();
    int           total      = 0;
    int           contentLen = http.getSize();
    unsigned long deadline   = millis() + HTTP_TIMEOUT_MS;

    while ((http.connected() || stream->available()) && millis() < deadline) {
        int avail = stream->available();
        if (avail > 0) {
            int toRead = min(avail, (int)(TILE_BUF_SIZE - total));
            if (toRead <= 0) break;
            total   += stream->readBytes(s_tileBuf + total, toRead);
            deadline = millis() + 5000;
        }
        if (contentLen > 0 && total >= contentLen) break;
        delay(1);
    }

    http.end();
    return total;
}

// ─── Tile URL builder ─────────────────────────────────────────────────────────

static String buildMapTileURL(int z, int x, int y) {
    return String("https://tiles.stadiamaps.com/tiles/")
        + STADIA_STYLE
        + "/" + String(z)
        + "/" + String(x)
        + "/" + String(y)
        + ".png?api_key=" + s_stadiaKey;
}

// ─── Tile fetch + decode ──────────────────────────────────────────────────────

static bool fetchAndDecodeTile(const String& url, int offX, int offY,
                                PNG_DRAW_CALLBACK* cb) {
    Serial.printf("[display] tile %s\n", url.c_str());

    int len = httpGetBinary(url);
    if (len == 0) return false;

    s_ctx.tileOffX = offX;
    s_ctx.tileOffY = offY;

    int rc = png.openRAM(s_tileBuf, len, cb);
    if (rc != PNG_SUCCESS) {
        Serial.printf("[display] PNG open failed: %d\n", rc);
        return false;
    }

    rc = png.decode(nullptr, 0);
    png.close();

    if (rc != PNG_SUCCESS) {
        Serial.printf("[display] PNG decode failed: %d\n", rc);
        return false;
    }
    return true;
}

// ─── Coordinate conversion ────────────────────────────────────────────────────

static bool latLonToPixel(float lat, float lon, const AppConfig& cfg,
                           int& px, int& py) {
    float dx_km = (lon - cfg.longitude) * cosf(cfg.latitude * (float)M_PI / 180.0f) * 111.320f;
    float dy_km = (lat - cfg.latitude) * 111.320f;
    float range_km = cfg.rangeMiles * 1.60934f;
    float scale = 120.0f / range_km;
    px = 120 + (int)(dx_km * scale);
    py = 120 - (int)(dy_km * scale);
    return (dx_km * dx_km + dy_km * dy_km) <= (range_km * range_km);
}

// ─── Plane triangle renderer ──────────────────────────────────────────────────

static void drawPlaneTriangle(int cx, int cy, float trackDeg, uint16_t color) {
    float theta = trackDeg * (float)M_PI / 180.0f;
    float c = cosf(theta), s = sinf(theta);

    // Unrotated: tip = (0, -6) pointing North (up), base corners at (-4, 4) and (4, 4).
    // Rotation x' = x*c - y*s, y' = x*s + y*c gives clockwise rotation on screen
    // because screen Y increases downward (verified: track=90 → tip points right).
    auto rx = [&](float x, float y) { return (int)(cx + x * c - y * s); };
    auto ry = [&](float x, float y) { return (int)(cy + x * s + y * c); };

    tft.fillTriangle(rx(0, -6), ry(0, -6),
                     rx(-4,  4), ry(-4,  4),
                     rx( 4,  4), ry( 4,  4),
                     color);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void initDisplay() {
    pinMode(LCD_BL_PIN, OUTPUT);
    digitalWrite(LCD_BL_PIN, HIGH);
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
}

void showStatus(const char* msg) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(msg, DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2, 2);
}

uint16_t* allocBackgroundBuffer() {
    uint16_t* buf = (uint16_t*)malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
    if (!buf) Serial.println("[display] background buffer alloc failed — using dark mode");
    return buf;
}

// Map zoom level from display radius in miles (each halving of range → +1 zoom).
// Baseline: 30 mi → zoom 9. Approximate; Stadia tiles at any zoom are usable.
static int rangeToZoom(int miles) {
    if (miles <= 4)  return 12;
    if (miles <= 8)  return 11;
    if (miles <= 16) return 10;
    if (miles <= 32) return 9;
    return 8;
}

bool fetchMapBackground(const AppConfig& cfg, uint16_t* buf) {
    strncpy(s_stadiaKey, cfg.stadiaKey, sizeof(s_stadiaKey) - 1);
    s_stadiaKey[sizeof(s_stadiaKey) - 1] = '\0';
    s_bgBuf = buf;

    int zoom = rangeToZoom(cfg.rangeMiles);

    int tileX = lon2tileX(cfg.longitude, zoom);
    int tileY = lat2tileY(cfg.latitude,  zoom);
    int subX  = lon2subPixel(cfg.longitude, zoom);
    int subY  = lat2subPixel(cfg.latitude,  zoom);

    int gridOriginX = (subX >= TILE_SIZE_PX / 2) ? tileX : tileX - 1;
    int gridOriginY = (subY >= TILE_SIZE_PX / 2) ? tileY : tileY - 1;

    int locPixX = (tileX - gridOriginX) * TILE_SIZE_PX + subX;
    int locPixY = (tileY - gridOriginY) * TILE_SIZE_PX + subY;

    s_ctx.cropX           = constrain(locPixX - DISPLAY_WIDTH  / 2, 0, STITCH_SIZE - DISPLAY_WIDTH);
    s_ctx.cropY           = constrain(locPixY - DISPLAY_HEIGHT / 2, 0, STITCH_SIZE - DISPLAY_HEIGHT);
    s_ctx.gridOriginTileX = gridOriginX;
    s_ctx.gridOriginTileY = gridOriginY;

    Serial.printf("[display] map zoom=%d  crop(%d,%d)  loc(%d,%d)\n",
                  zoom, s_ctx.cropX, s_ctx.cropY, locPixX, locPixY);

    // Pre-fill buffer with black so any un-decoded region is blank
    memset(buf, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));

    int tilesOK = 0;
    for (int row = 0; row < TILE_GRID; row++) {
        for (int col = 0; col < TILE_GRID; col++) {
            String url = buildMapTileURL(zoom, gridOriginX + col, gridOriginY + row);
            if (fetchAndDecodeTile(url, col * TILE_SIZE_PX, row * TILE_SIZE_PX,
                                   mapToBufferCallback)) {
                tilesOK++;
            }
        }
    }

    s_bgBuf = nullptr;
    Serial.printf("[display] map background fetched (%d/4 tiles OK)\n", tilesOK);
    return tilesOK > 0;
}

void drawDarkBackground() {
    uint16_t gray = tft.color565(60, 60, 60);
    tft.fillScreen(TFT_BLACK);
    tft.drawCircle(120, 120,  40, gray);   // inner ring  (~1/3 range)
    tft.drawCircle(120, 120,  80, gray);   // outer ring  (~2/3 range)
    tft.drawCircle(120, 120, 120, gray);   // clip boundary
    tft.fillCircle(120, 120,   2, gray);   // center dot
}

void drawPlanes(const AppConfig& cfg, const PlaneState* planes, int count,
                uint16_t* bgBuf) {
    // Defensive re-init: recovers the GC9A01 if it lost its state (e.g. an
    // electrical glitch reset the controller) without needing a full reboot.
    initDisplay();

    if (cfg.bgMode == BG_MAP && bgBuf) {
        tft.pushImage(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, bgBuf);
    } else {
        drawDarkBackground();
    }

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);  // opaque bg prevents map bleed

    for (int i = 0; i < count; i++) {
        int px, py;
        if (!latLonToPixel(planes[i].lat, planes[i].lon, cfg, px, py)) continue;
        float track = planes[i].track;
        drawPlaneTriangle(px, py, track, TFT_YELLOW);

        int lineY = py + 8;  // Font 1 = 5×7 px
        if (cfg.showCallsign) {
            tft.drawString(planes[i].callsign, px, lineY, 1);
            lineY += 8;
        }
        if (cfg.showModel) {
            const char* text;
            if (planes[i].typeCode[0] == '\0') {
                text = "?";
            } else if (cfg.modelFormat == MODEL_FMT_NAME) {
                const char* name = lookupTypeName(planes[i].typeCode);
                text = name ? name : planes[i].typeCode;  // fall back to the raw code
            } else {
                text = planes[i].typeCode;
            }
            tft.drawString(text, px, lineY, 1);
        }
    }

    // Range label — bottom of circle; "(a)" suffix + blue when auto-zoom is
    // on, dim gray otherwise
    char rangeLabel[16];
    if (cfg.autoZoom) snprintf(rangeLabel, sizeof(rangeLabel), "%d mi (a)", cfg.rangeMiles);
    else              snprintf(rangeLabel, sizeof(rangeLabel), "%d mi",     cfg.rangeMiles);
    tft.setTextDatum(BC_DATUM);
    uint16_t labelColor = cfg.autoZoom
        ? tft.color565(96, 165, 250)    // #60a5fa — soft blue
        : tft.color565(120, 120, 120);  // dim gray
    tft.setTextColor(labelColor, TFT_BLACK);
    tft.drawString(rangeLabel, 120, 223, 1);  // Font 1, near bottom of 240px circle
}
