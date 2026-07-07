#include "imu.h"
#include "config.h"
#include <Wire.h>

#define REG_WHO_AM_I   0x00
#define REG_CTRL2      0x03   // Accelerometer: full-scale + ODR
#define REG_CTRL7      0x08   // Enable sensors
#define REG_CTRL8      0x09   // Motion engine enable (bit 0 = tap)
#define REG_CTRL9      0x0A   // Command register
#define REG_CAL1_L     0x0B
#define REG_CAL1_H     0x0C
#define REG_CAL2_L     0x0D
#define REG_CAL2_H     0x0E
#define REG_CAL3_L     0x0F
#define REG_CAL3_H     0x10
#define REG_CAL4_H     0x12
#define REG_STATUSINT  0x2D   // bit 7 = CTRL9 command done
#define REG_TAP_STATUS 0x59   // bits[1:0]: 0=none, 1=single, 2=double

static uint8_t readReg(uint8_t reg) {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(IMU_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

static void writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

// Wait for CTRL9 command to complete (STATUS_INT bit 7), then ACK with 0x00.
static void ctrl9Ack() {
    int i;
    for (i = 0; i < 200; i++) {
        if (readReg(REG_STATUSINT) & 0x80) break;
        delay(1);
    }
    if (i == 200) Serial.println("[imu] WARN: CTRL9 ack timed out");
    writeReg(REG_CTRL9, 0x00);  // clear command / acknowledge
}

bool checkFaceDown() {
    Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN);

    uint8_t whoami = readReg(REG_WHO_AM_I);
    if (whoami != 0x05) {
        Serial.printf("[imu] WHO_AM_I=0x%02X — skipping face-down check\n", whoami);
        Wire.end();
        return false;
    }

    writeReg(REG_CTRL2, 0x23);  // ±8 g, 59 Hz ODR
    writeReg(REG_CTRL7, 0x01);  // enable accelerometer
    delay(50);                   // wait for first sample (~17 ms at 59 Hz)

    Wire.beginTransmission(IMU_ADDR);
    Wire.write(0x39);            // REG_AZ_L (0x35 = AX_L, wrong axis)
    Wire.endTransmission(false);
    Wire.requestFrom(IMU_ADDR, (uint8_t)2);

    int16_t az_raw = 0;
    if (Wire.available() >= 2)
        az_raw = (int16_t)(Wire.read() | (Wire.read() << 8));

    writeReg(REG_CTRL7, 0x00);  // disable before initTapDetection re-enables
    Wire.end();

    float az_g = az_raw / 4096.0f;
    Serial.printf("[imu] az=%.3f g  raw=%d\n", az_g, az_raw);
    return az_g < -0.7f;
}

void initTapDetection() {
    Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN);

    uint8_t whoami = readReg(REG_WHO_AM_I);
    if (whoami != 0x05) {
        Serial.printf("[imu] WHO_AM_I=0x%02X — tap detection disabled\n", whoami);
        return;
    }

    // Sensors must be off while writing tap parameters.
    writeReg(REG_CTRL7, 0x00);

    // ── Phase 1: timing parameters ────────────────────────────────────────────
    // Values scaled for 59 Hz ODR (1 sample ≈ 17 ms).
    // At 500 Hz the library uses peakWindow=20, tapWindow=50, dTapWindow=250.
    // Scale down proportionally: ×(59/500) ≈ ×0.12.
    const uint8_t  peakWindow  = 3;    // samples to find peak within (~51 ms)
    const uint8_t  priority    = 2;    // Z-axis priority (device tapped face/back)
    const uint16_t tapWindow   = 6;    // max tap duration in samples (~102 ms)
    const uint16_t dTapWindow  = 30;   // max inter-tap gap for double (~510 ms)

    writeReg(REG_CAL1_L, peakWindow);
    writeReg(REG_CAL1_H, priority);
    writeReg(REG_CAL2_L,  tapWindow & 0xFF);
    writeReg(REG_CAL2_H,  tapWindow >> 8);
    writeReg(REG_CAL3_L, dTapWindow & 0xFF);
    writeReg(REG_CAL3_H, dTapWindow >> 8);
    writeReg(REG_CAL4_H, 0x01);        // phase 1 marker

    writeReg(REG_CTRL9, 0x0C);         // CTRL_CMD_CONFIGURE_TAP
    ctrl9Ack();

    // ── Phase 2: algorithm thresholds ─────────────────────────────────────────
    // alpha/gamma: fixed-point ×128.  Thresholds: units of 0.001 g².
    // Lowered from 800/400 → 400/200 for more reliable detection of normal taps.
    const uint8_t  alpha      = 8;     // 0.0625 × 128
    const uint8_t  gamma      = 32;    // 0.25   × 128
    const uint16_t peakMagThr = 400;   // 0.4 g²  / 0.001 g²  per unit
    const uint16_t udmThr     = 200;   // 0.2 g²  / 0.001 g²  per unit

    writeReg(REG_CAL1_L, alpha);
    writeReg(REG_CAL1_H, gamma);
    writeReg(REG_CAL2_L, peakMagThr & 0xFF);
    writeReg(REG_CAL2_H, peakMagThr >> 8);
    writeReg(REG_CAL3_L, udmThr & 0xFF);
    writeReg(REG_CAL3_H, udmThr >> 8);
    writeReg(REG_CAL4_H, 0x02);        // phase 2 marker

    writeReg(REG_CTRL9, 0x0C);         // CTRL_CMD_CONFIGURE_TAP (second phase)
    ctrl9Ack();

    // Enable accelerometer and tap detection engine.
    writeReg(REG_CTRL2, 0x23);         // ±8 g, 59 Hz ODR
    writeReg(REG_CTRL7, 0x01);         // enable accelerometer
    writeReg(REG_CTRL8, 0x01);         // bit 0: activate tap detection

    // Allow tap engine to settle before the first poll.
    delay(500);
    Serial.println("[imu] tap detection initialised");
}

uint8_t pollTap() {
    // STATUS1 (0x2F) bit 1 = TAP_MOTION: latched interrupt flag.
    // Must be read BEFORE TAP_STATUS to gate spurious reads, then it self-clears.
    if (!(readReg(0x2F) & 0x02)) return TAP_NONE;

    // TAP_STATUS (0x59) bits[1:0]: 0=none, 1=single, 2=double. Self-clears on read.
    uint8_t t = readReg(REG_TAP_STATUS) & 0x03;
    if (t == 2) { Serial.println("[imu] double-tap"); return TAP_DOUBLE; }
    if (t == 1) { Serial.println("[imu] single-tap");  return TAP_SINGLE; }
    return TAP_NONE;
}
