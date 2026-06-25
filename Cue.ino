/*
 * ============================================================================
 *  cue-new  —  SSD1309 128x64 OLED bring-up spike for Cue (Arduino + U8g2)
 * ============================================================================
 *  Board : ESP32-S3 N16R8 (YD-ESP32-S3)
 *  Panel : 2.42" SSD1309 128x64, I2C, address 0x3C
 *  Lib   : U8g2 by olikraus (install via Library Manager or:
 *              arduino-cli lib install "U8g2")
 *
 *  PURPOSE
 *  -------
 *  Prove the OLED is alive and correctly wired BEFORE trusting the
 *  hand-rolled ESP-IDF driver in
 *      Cue/repos/Cue/components/display/ssd1309.c
 *  This is the OLED equivalent of the ../not-cue e-paper spike: smallest
 *  possible program that answers "does the panel light up on these pins?".
 *
 *  Pins come straight from Cue's board.h:
 *      PIN_I2C_SDA = GPIO7
 *      PIN_I2C_SCL = GPIO14
 *      DISPLAY_I2C_ADDR = 0x3C
 *      I2C_HZ = 400000
 *
 *  WIRING (OLED -> ESP32-S3):
 *      VCC -> 3V3            <-- 3.3 V (most 2.42" SSD1309 boards accept 3-5V,
 *                                but the ESP32 logic is 3.3 V)
 *      GND -> GND
 *      SDA -> GPIO7
 *      SCL -> GPIO14
 *  Nothing else is connected.
 *
 *  WHAT YOU SHOULD SEE
 *  -------------------
 *   - Serial @115200 prints an I2C probe result for 0x3C (ACK = found).
 *   - The panel shows a framed "Cue OLED OK" test screen with the pin map
 *     and a moving marker so you can confirm it is live, not a frozen image.
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ---- Pins (temporary test pair: untried pads) --------------------------------
#define OLED_SDA 15
#define OLED_SCL 16
#define OLED_ADDR 0x3C // DISPLAY_I2C_ADDR
// 100kHz standard-mode while bringing the bus up. REQUIRES external pull-ups
// (SDA->3V3, SCL->3V3); with none, only the ESP32 internal ~45k holds the lines
// and the slow rise time corrupts pixel data -> stable "barcode" (panel still
// ACKs because the lone address byte survives). 4.7k pull-ups -> can raise to
// 400000 to match the production ssd1309.c driver.
#define OLED_HZ 100000 // I2C_HZ

// ----------------------------------------------------------------------------
//  DISPLAY DRIVER SELECTION
//  ---------------------------------------------------------------------------
//  PRIMARY: the panel is an SSD1309. Full-buffer ("_F_") hardware-I2C ctor.
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

//  ---- ALTERNATIVES --------------------------------------------------------
//  If this gives a blank / garbled / shifted image, comment out the ctor above
//  and UN-comment exactly ONE of these, then re-flash.
//
//  // SSD1309 alternate panel variant:
//  U8G2_SSD1309_128X64_NONAME2_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
//
//  // Treat it as a plain SSD1306 (the init sequences are compatible; Cue's
//  // own ssd1309.c uses an SSD1306-style sequence):
//  U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// ----------------------------------------------------------------------------

// Raw I2C probe so we get a clear ACK/NACK independent of the graphics lib.
// Mirrors the i2c_scan() spirit in Cue's main.c. Returns true if the address
// ACKs on the bus.
static bool i2cProbe(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0); // 0 == device ACKed
}

// Full I2C bus scan (0x03..0x77). Logs every address that responds, so a
// mis-set address still shows up instead of just "not found".
static void i2cScan(void)
{
    Serial.printf("[oled] I2C scan on SDA=%d SCL=%d:\n", OLED_SDA, OLED_SCL);
    int found = 0;
    for (uint8_t a = 0x03; a <= 0x77; a++)
    {
        if (i2cProbe(a))
        {
            Serial.printf("[oled]   ACK @ 0x%02X%s\n", a,
                          (a == OLED_ADDR) ? "  <- OLED" : "");
            found++;
        }
    }
    if (!found)
        Serial.println(F("[oled]   no devices responded (check VCC/GND/SDA/SCL)"));
}

// Draw the static test screen. U8g2 full-buffer: build the whole frame between
// firstPage()/nextPage(). `tick` drives a small moving marker so the screen is
// visibly live rather than a frozen splash.
static void drawTestScreen(uint32_t tick)
{
    u8g2.firstPage();
    do
    {
        u8g2.drawFrame(0, 0, 128, 64); // outer border

        u8g2.setFont(u8g2_font_ncenB10_tr); // ~10px bold
        u8g2.drawStr(8, 16, "WOO!!");

        u8g2.setFont(u8g2_font_5x7_tr); // tiny details
        u8g2.drawStr(6, 30, "SSD1309 128x64 @0x3C");
        char pins[32];
        snprintf(pins, sizeof(pins), "SDA=GPIO%d  SCL=GPIO%d", OLED_SDA, OLED_SCL);
        u8g2.drawStr(6, 40, pins);
        u8g2.drawStr(6, 50, "I2C 100kHz  spike test");

        // moving marker: a filled box that steps across the bottom each refresh.
        int16_t x = 6 + (int16_t)(tick % 110);
        u8g2.drawBox(x, 56, 6, 6);
    } while (u8g2.nextPage());
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("[oled] Cue SSD1309 OLED bring-up spike"));

    // Bring up I2C on Cue's pins, then probe before drawing.
    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(OLED_HZ);
    i2cScan();

    if (i2cProbe(OLED_ADDR))
        Serial.printf("[oled] 0x%02X ACK -> initializing U8g2\n", OLED_ADDR);
    else
        Serial.printf("[oled] 0x%02X did NOT ACK -> will still try init; "
                      "check wiring if screen stays blank\n",
                      OLED_ADDR);

    // U8g2 drives the same Wire bus; set the 7-bit address and start.
    // setBusClock() MUST be called before begin() — begin() otherwise uses
    // U8g2's own default (400kHz) and ignores Wire.setClock() above.
    u8g2.setI2CAddress(OLED_ADDR << 1); // U8g2 wants the 8-bit form
    u8g2.setBusClock(OLED_HZ);
    u8g2.begin();
    u8g2.setContrast(255);

    Serial.println(F("[oled] init done. Drawing test screen."));
}

void loop()
{
    // Redraw ~2x/sec with a stepping marker so the panel is visibly alive.
    static uint32_t tick = 0;
    drawTestScreen(tick++);
    Serial.printf("[oled] frame %lu drawn\n", (unsigned long)tick);
    delay(500);
}
