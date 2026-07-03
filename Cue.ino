/*
 * ============================================================================
 *  cue-new  —  STEP 2: bare OLED bring-up for Cue (Arduino + U8g2)
 * ============================================================================
 *  Board : ESP32 DevKitV1 (ESP32-WROOM-32, classic dual-core, 4MB flash)
 *  Panel : 2.42" SSD1309 128x64, I2C, address 0x3C
 *  Lib   : U8g2 by olikraus  (make deps  ->  arduino-cli lib install "U8g2")
 *
 *  PURPOSE
 *  -------
 *  Step 1 proved the bare board boots (LED blink + serial heartbeat). This is
 *  Step 2: add the OLED with the MINIMUM 4 wires and see how far it gets with
 *  NO pull-up resistors and NO RES jumper yet — exactly as much hardware as
 *  has been deemed necessary so far, nothing more.
 *
 *  WIRING (OLED -> ESP32 DevKitV1) — bare, 4 wires only:
 *      VCC -> 3V3
 *      GND -> GND
 *      SDA -> GPIO21
 *      SCL -> GPIO22
 *  NOT connected yet (add ONLY if the steps below say we need them):
 *      RES -> (floating for now; our notes warn this can blank the panel)
 *      pull-up resistors on SDA/SCL -> none
 *
 *  STRATEGY (keep proof-of-life alive the whole time)
 *  --------------------------------------------------
 *   - The onboard LED keeps blinking and serial keeps a heartbeat, so even if
 *     the panel is dark we still know the board runs and can read the scan.
 *   - Every second we run an I2C scan and print what ACKs. This is the real
 *     signal: it tells us if the panel is electrically present BEFORE we worry
 *     about pixels.
 *   - FLICKER-FREE ANIMATION: full-buffer redraws every frame made the panel
 *     shimmer (proven by a draw-once test). So we push the whole screen ONCE,
 *     then animate the moving marker with updateDisplayArea() on just the
 *     bottom 8px tile strip — the text rows are never retransmitted.
 *
 *  WHAT YOU SHOULD SEE
 *  -------------------
 *   A) Best case: "[oled] ACK @ 0x3C  <- OLED", then a framed "WOO!!" test
 *      screen with a moving box, plus the LED blinking.
 *   B) Board fine but panel absent on the bus: heartbeat + "no devices
 *      responded" each scan -> next step is RES->3V3, then pull-ups.
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// On-board LED on most ESP32 DevKitV1 boards is wired to GPIO2.
#define LED_PIN 2

// ---- I2C pins (classic ESP32 hardware-I2C default pair, non-strapping) -------
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDR 0x3C
// 400 kHz fast-mode. At 100 kHz the full-frame transfer took ~90ms and the
// repaint was visible as flicker; 400 kHz cuts that ~4x so the redraw is too
// quick to see. NO external pull-ups yet, so the faster edges rely on the
// ESP32 internal ~45k. If this shows a garbled "barcode", THAT is the proof
// pull-ups (SDA->3V3, SCL->3V3) are finally needed -> then drop back if so.
#define OLED_HZ 400000

// SSD1309, full-buffer ("_F_"), hardware I2C. reset=NONE because RES is not
// wired yet; U8g2 won't toggle a reset line it doesn't have.
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

static bool g_oledPresent = false; // set true once 0x3C ACKs; gates drawing.

// Raw I2C probe (ACK/NACK) independent of the graphics lib.
static bool i2cProbe(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0); // 0 == device ACKed
}

// Scan 0x03..0x77 and log every responder, so a mis-set address still shows up.
// Returns true if the OLED address specifically ACKed.
static bool i2cScan(void)
{
    Serial.printf("[oled] I2C scan on SDA=%d SCL=%d @ %d Hz:\n",
                  OLED_SDA, OLED_SCL, OLED_HZ);
    int found = 0;
    bool oled = false;
    for (uint8_t a = 0x03; a <= 0x77; a++)
    {
        if (i2cProbe(a))
        {
            bool isOled = (a == OLED_ADDR);
            Serial.printf("[oled]   ACK @ 0x%02X%s\n", a, isOled ? "  <- OLED" : "");
            found++;
            oled |= isOled;
        }
    }
    if (!found)
        Serial.println(F("[oled]   no devices responded "
                         "(bare bus: try RES->3V3, then pull-ups)"));
    return oled;
}

// Marker geometry. The moving box lives entirely within the bottom 8-pixel
// tile row (y 56..63), which is what lets us update JUST that strip.
#define MARKER_Y 56
#define MARKER_W 6
#define MARKER_H 6
#define MARKER_X_MIN 6
#define MARKER_X_SPAN 110 // marker sweeps MARKER_X_MIN .. MARKER_X_MIN+SPAN-1

// Draw the FULL static screen (frame + text + marker) into the buffer and push
// it ONCE. After this we never send the whole buffer again — only the marker
// strip changes (see updateMarker), which is what keeps it flicker-free.
static void drawStaticScreen(int16_t markerX)
{
    u8g2.clearBuffer();

    u8g2.drawFrame(0, 0, 128, 64);

    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(8, 16, "WOO!!");

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(6, 30, "SSD1309 128x64 @0x3C");
    char pins[32];
    snprintf(pins, sizeof(pins), "SDA=GPIO%d  SCL=GPIO%d", OLED_SDA, OLED_SCL);
    u8g2.drawStr(6, 40, pins);
    u8g2.drawStr(6, 50, "bare bring-up  400kHz");

    u8g2.drawBox(markerX, MARKER_Y, MARKER_W, MARKER_H);

    u8g2.sendBuffer(); // single full-buffer push
}

// Move the marker WITHOUT re-sending the whole frame. We only rewrite the
// bottom tile row (y 56..63) in the buffer and transmit just those 16x1 tiles
// via updateDisplayArea() — ~128 bytes instead of the full 1KB. The text rows
// above are never touched, so there is nothing to flicker.
static void updateMarker(int16_t markerX)
{
    // Erase the whole bottom tile strip in the buffer...
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, MARKER_Y, 128, 8);
    u8g2.setDrawColor(1);

    // ...restore the frame border segments that live in this strip...
    u8g2.drawVLine(0, MARKER_Y, 8);   // left edge
    u8g2.drawVLine(127, MARKER_Y, 8); // right edge
    u8g2.drawHLine(0, 63, 128);       // bottom edge

    // ...draw the marker at its new position...
    u8g2.drawBox(markerX, MARKER_Y, MARKER_W, MARKER_H);

    // ...and send ONLY tile row 7 (x tiles 0..15 = full width, y tile 7 = y56..63).
    u8g2.updateDisplayArea(0, 7, 16, 1);
}

void setup()
{
    pinMode(LED_PIN, OUTPUT);

    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F("[boot] Cue STEP 2 — bare OLED bring-up"));
    Serial.printf("[boot] chip: %s rev %d, %d core(s)\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
    Serial.printf("[boot] free heap: %d bytes\n", ESP.getFreeHeap());

    // Bring up the I2C bus on the OLED pins and scan once at boot.
    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(OLED_HZ);
    g_oledPresent = i2cScan();

    if (g_oledPresent)
    {
        // setBusClock() MUST precede begin(): begin() otherwise uses U8g2's own
        // 400kHz default and ignores Wire.setClock() above.
        u8g2.setI2CAddress(OLED_ADDR << 1); // U8g2 wants the 8-bit form
        u8g2.setBusClock(OLED_HZ);
        u8g2.begin();
        u8g2.setContrast(255);
        Serial.println(F("[oled] 0x3C present -> U8g2 initialized, drawing."));
    }
    else
    {
        Serial.println(F("[oled] 0x3C absent -> board stays alive; "
                         "re-scanning each second (check wiring / RES / pull-ups)."));
    }

    Serial.println(F("[boot] setup done"));
    Serial.println(F("========================================"));
}

void loop()
{
    static uint32_t tick = 0;

    // Non-blocking heartbeat: toggle the LED on a ~1Hz schedule using millis()
    // instead of delay(), so nothing stalls the display refresh (the old
    // delay()s between blinks were part of the visible flicker).
    static uint32_t lastBlink = 0;
    static bool led = false;
    uint32_t now = millis();
    if (now - lastBlink >= 500)
    {
        lastBlink = now;
        led = !led;
        digitalWrite(LED_PIN, led);
    }

    if (g_oledPresent)
    {
        // Draw the full static screen exactly once...
        static bool drawnOnce = false;
        if (!drawnOnce)
        {
            drawnOnce = true;
            drawStaticScreen(MARKER_X_MIN);
            Serial.println(F("[oled] static frame pushed once; "
                             "marker now animates via partial updates."));
        }

        // ...then animate the marker with partial (bottom-strip-only) updates.
        // Only send when it steps to a new column, so the bus stays quiet
        // between moves and the text above is never retransmitted (no flicker).
        int16_t markerX = MARKER_X_MIN + (int16_t)((millis() / 40) % MARKER_X_SPAN);
        static int16_t lastMarkerX = -1;
        if (markerX != lastMarkerX)
        {
            lastMarkerX = markerX;
            updateMarker(markerX);
            if ((tick % 60) == 0) // occasional heap log, not per-frame spam
                Serial.printf("[oled] marker step %lu   heap=%d\n",
                              (unsigned long)tick, ESP.getFreeHeap());
            tick++;
        }
    }
    else
    {
        // Not present yet: keep probing so plugging RES/pull-ups in is detected
        // live without a re-flash. If it appears, init and switch to drawing.
        Serial.printf("[alive] tick %lu   heap=%d\n",
                      (unsigned long)tick, ESP.getFreeHeap());
        if (i2cScan())
        {
            g_oledPresent = true;
            u8g2.setI2CAddress(OLED_ADDR << 1);
            u8g2.setBusClock(OLED_HZ);
            u8g2.begin();
            u8g2.setContrast(255);
            Serial.println(F("[oled] 0x3C just appeared -> initialized, drawing."));
        }
        tick++;
        delay(500); // only throttle the scan path, not the drawing path
    }
}
