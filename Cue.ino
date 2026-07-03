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
 *   - Only after 0x3C ACKs do we init U8g2 and draw. If it never ACKs, the
 *     serial log tells us whether to add RES-high or pull-ups next.
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
// 100 kHz standard-mode for bring-up. With NO external pull-ups the bus is held
// up only by the ESP32's internal ~45k, so rise time is slow -> keep it slow.
// If the panel ACKs but the image is a garbled "barcode", THAT is the cue that
// real pull-ups (SDA->3V3, SCL->3V3) are finally needed; raise to 400000 then.
#define OLED_HZ 100000

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

// Build one full frame. `tick` drives a moving marker so a live panel is
// obviously live and not a frozen splash.
static void drawTestScreen(uint32_t tick)
{
    u8g2.firstPage();
    do
    {
        u8g2.drawFrame(0, 0, 128, 64);

        u8g2.setFont(u8g2_font_ncenB10_tr);
        u8g2.drawStr(8, 16, "WOO!!");

        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(6, 30, "SSD1309 128x64 @0x3C");
        char pins[32];
        snprintf(pins, sizeof(pins), "SDA=GPIO%d  SCL=GPIO%d", OLED_SDA, OLED_SCL);
        u8g2.drawStr(6, 40, pins);
        u8g2.drawStr(6, 50, "bare bring-up  100kHz");

        int16_t x = 6 + (int16_t)(tick % 110);
        u8g2.drawBox(x, 56, 6, 6);
    } while (u8g2.nextPage());
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

    // Heartbeat: LED + serial, so proof-of-life survives even with a dark panel.
    digitalWrite(LED_PIN, HIGH);
    delay(250);
    digitalWrite(LED_PIN, LOW);
    delay(250);

    if (g_oledPresent)
    {
        drawTestScreen(tick);
        Serial.printf("[oled] frame %lu drawn   heap=%d\n",
                      (unsigned long)tick, ESP.getFreeHeap());
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
    }

    tick++;
    delay(500);
}
