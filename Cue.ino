/*
 * ============================================================================
 *  cue-new  —  STEP 3: microSD bring-up (music library) for Cue
 * ============================================================================
 *  Board : ESP32 DevKitV1 / ELEGOO ESP32 (ESP32-WROOM-32, classic, 4MB flash)
 *  Panel : 2.42" SSD1309 128x64 OLED, I2C 0x3C  (proven in Step 2)
 *  Card  : generic 6-pin SPI microSD breakout, card formatted FAT32
 *  Libs  : U8g2 (display) + SD/SPI/FS (bundled with the ESP32 Arduino core)
 *
 *  PURPOSE
 *  -------
 *  Steps 1-2 proved the board + OLED. Step 3 adds the microSD card that will
 *  hold Cue's music library. This spike mounts the card and lists the .mp3
 *  files (ignoring any non-mp3 files) on BOTH serial and the OLED — proving the
 *  storage path before we add MP3 decode + Bluetooth A2DP streaming.
 *
 *  Recipe reused from the PocketPage project (stock Arduino SD.h) but on a
 *  DEDICATED SPI bus: PocketPage shared its bus with an e-paper panel; Cue's
 *  display is I2C, so the SD card owns the SPI bus here (simpler + faster).
 *
 *  WIRING — OLED (I2C, unchanged from Step 2):
 *      VCC->3V3  GND->GND  SDA->GPIO21  SCL->GPIO22
 *  WIRING — microSD (dedicated SPI):
 *      3V3->3V3  GND->GND  CLK->GPIO18  MOSI->GPIO23  MISO->GPIO19  CS->GPIO4
 *      CS=GPIO4 dodges the OLED pins (21/22) and the strapping pins
 *      (0,2,5,12,15). Add a 10uF cap at the SD 3V3 pad (PocketPage lesson).
 *
 *  WHAT YOU SHOULD SEE
 *  -------------------
 *   - Serial: "[sd] OK: SDHC/XC, 31000 MB, N mp3 file(s)" then the names.
 *   - OLED: an "SD SCAN" screen with card type/size, mp3 count, first names.
 *   - Onboard LED keeps blinking (proof-of-life) regardless of SD result.
 *   - Mount fail? Serial + OLED say so and list the wiring to check.
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "FS.h"
#include "SD.h"
#include "BluetoothA2DPSource.h"
#include <esp_log.h>

// On-board LED on most ESP32 DevKitV1 boards is wired to GPIO2.
#define LED_PIN 2

// ---- I2C pins (OLED — proven in Step 2) -------------------------------------
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDR 0x3C
#define OLED_HZ 400000 // 400kHz fast-mode; proven flicker-free in Step 2

// ---- SPI pins (microSD — dedicated bus, no sharing) -------------------------
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS 4
// Start conservative. PocketPage used 1 MHz over long SHARED soldered leads;
// Cue's SD owns a short DEDICATED bus, so 4 MHz is a safe start and can be
// raised later once the path is proven.
#define SD_HZ 4000000

// SSD1309, full-buffer ("_F_"), hardware I2C. reset=NONE (RES not wired).
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

static bool g_oledPresent = false; // set true once 0x3C ACKs; gates drawing.

// ---- microSD scan results (gathered once at boot) ---------------------------
static bool g_sdMounted = false;
static const char *g_sdType = "?";
static float g_sdSizeMB = 0;
static int g_mp3Count = 0;   // total .mp3 files found
static String g_mp3Names[5]; // first few names, for the OLED/serial
static int g_mp3Shown = 0;   // how many of g_mp3Names are filled

// ---- Bluetooth A2DP source (streams audio OUT to a BT speaker/headphone) -----
// Cue is the SOURCE: it connects TO this sink by its advertised name (exact,
// case-sensitive). Sub-step 1 streams a sine tone to prove the radio path;
// sub-step 2 will replace the tone with decoded MP3 from the SD card.
#define BT_SINK_NAME "Moondrop Space Travel"
static BluetoothA2DPSource a2dp_source;
static volatile bool g_btConnected = false;

// ---- Input: 2 rotary encoders (A/B/SW) + 4 keyboard switches ----------------
// All active-low with internal pull-ups (INPUT_PULLUP): idle HIGH, pressed LOW.
// Encoder commons -> GND; each switch's other leg -> GND. No external resistors.
#define ENC1_A 32
#define ENC1_B 33
#define ENC1_SW 25
#define ENC2_A 26
#define ENC2_B 27
#define ENC2_SW 14
#define SW1_PIN 13
#define SW2_PIN 16
#define SW3_PIN 17
// GPIO15 is a STRAPPING pin. INPUT_PULLUP idles it HIGH (its required boot
// level), so it's safe here; holding SW4 during power-on only mutes the
// boot-ROM debug log, which is harmless.
#define SW4_PIN 15

#define DEBOUNCE_MS 25

// TEMP: set to 1 to print every raw A/B transition on the encoders (diagnoses
// wiring vs decode). Set back to 0 once the encoders are confirmed working.
#define ENC_DEBUG 0

// Quadrature transition table. Index = (prev<<2)|curr, each 2 bits = (A<<1)|B.
// Value = direction of that Gray-code transition (-1/0/+1 quarter-step).
static const int8_t QUAD_LUT[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0};

struct Encoder
{
    uint8_t pinA, pinB;
    uint8_t prev; // last (A<<1)|B reading
    int8_t accum; // accumulated quarter-steps; one detent = 4
};

struct Button
{
    uint8_t pin;
    const char *name;
    bool stable;       // debounced level (HIGH = released)
    bool lastRead;     // last raw read
    uint32_t lastEdge; // millis() of last raw change
};

static Encoder g_enc1 = {ENC1_A, ENC1_B, 0, 0};
static Encoder g_enc2 = {ENC2_A, ENC2_B, 0, 0};
static int32_t g_enc1Pos = 0, g_enc2Pos = 0;

static Button g_buttons[] = {
    {ENC1_SW, "ENC1_SW", HIGH, HIGH, 0},
    {ENC2_SW, "ENC2_SW", HIGH, HIGH, 0},
    {SW1_PIN, "SW1", HIGH, HIGH, 0},
    {SW2_PIN, "SW2", HIGH, HIGH, 0},
    {SW3_PIN, "SW3", HIGH, HIGH, 0},
    {SW4_PIN, "SW4", HIGH, HIGH, 0},
};
static const int NUM_BUTTONS = sizeof(g_buttons) / sizeof(g_buttons[0]);

// Poll one encoder; returns -1/0/+1 DETENTS since the last call.
static int8_t encoderPoll(Encoder &e)
{
    uint8_t curr = (digitalRead(e.pinA) << 1) | digitalRead(e.pinB);
#if ENC_DEBUG
    if (curr != e.prev)
        Serial.printf("[enc-dbg] A(GPIO%u)=%u B(GPIO%u)=%u\n",
                      e.pinA, (curr >> 1) & 1, e.pinB, curr & 1);
#endif
    int8_t d = QUAD_LUT[(e.prev << 2) | curr];
    e.prev = curr;
    if (d == 0)
        return 0;
    e.accum += d;
    if (e.accum >= 4)
    {
        e.accum = 0;
        return +1;
    }
    if (e.accum <= -4)
    {
        e.accum = 0;
        return -1;
    }
    return 0;
}

// Debounced press detector; returns true ONCE per fresh press (HIGH->LOW).
static bool buttonPressed(Button &b)
{
    bool raw = digitalRead(b.pin);
    uint32_t now = millis();
    if (raw != b.lastRead)
    {
        b.lastRead = raw;
        b.lastEdge = now;
    }
    if ((now - b.lastEdge) >= DEBOUNCE_MS && raw != b.stable)
    {
        b.stable = raw;
        if (b.stable == LOW)
            return true; // just became pressed
    }
    return false;
}

// Configure all input pins and seed the encoder states.
static void inputInit(void)
{
    pinMode(ENC1_A, INPUT_PULLUP);
    pinMode(ENC1_B, INPUT_PULLUP);
    pinMode(ENC2_A, INPUT_PULLUP);
    pinMode(ENC2_B, INPUT_PULLUP);
    for (int i = 0; i < NUM_BUTTONS; i++)
        pinMode(g_buttons[i].pin, INPUT_PULLUP);

    g_enc1.prev = (digitalRead(ENC1_A) << 1) | digitalRead(ENC1_B);
    g_enc2.prev = (digitalRead(ENC2_A) << 1) | digitalRead(ENC2_B);

    Serial.println(F("[input] 2 encoders + 6 buttons ready "
                     "(INPUT_PULLUP, active-low)."));
}

// Poll every input and print any activity on serial. Must run every loop pass.
static void inputPoll(void)
{
    int8_t d1 = encoderPoll(g_enc1);
    if (d1)
    {
        g_enc1Pos += d1;
        Serial.printf("[input] ENC1 %+d -> pos %ld\n", d1, (long)g_enc1Pos);
    }
    int8_t d2 = encoderPoll(g_enc2);
    if (d2)
    {
        g_enc2Pos += d2;
        Serial.printf("[input] ENC2 %+d -> pos %ld\n", d2, (long)g_enc2Pos);
    }
    for (int i = 0; i < NUM_BUTTONS; i++)
        if (buttonPressed(g_buttons[i]))
            Serial.printf("[input] %s pressed\n", g_buttons[i].name);
}

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

// Mount the microSD card on the dedicated SPI bus and collect a summary plus
// the first few .mp3 filenames. Modeled on PocketPage's scanCard(), filtered
// for .mp3 and ignoring everything else on the card.
static void sdScan(void)
{
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    if (!SD.begin(SD_CS, SPI, SD_HZ))
    {
        g_sdMounted = false;
        Serial.println(F("[sd] mount FAILED -> check CLK18 MOSI23 MISO19 CS4, "
                         "3V3, GND; is the card FAT32?"));
        return;
    }

    uint8_t t = SD.cardType();
    if (t == CARD_NONE)
    {
        g_sdMounted = false;
        SD.end();
        Serial.println(F("[sd] no card detected"));
        return;
    }

    g_sdMounted = true;
    g_sdType = (t == CARD_MMC) ? "MMC" : (t == CARD_SD) ? "SDSC"
                                     : (t == CARD_SDHC) ? "SDHC/XC"
                                                        : "?";
    g_sdSizeMB = SD.cardSize() / (1024.0 * 1024.0);

    File root = SD.open("/");
    if (root)
    {
        for (File e = root.openNextFile(); e; e = root.openNextFile())
        {
            if (!e.isDirectory())
            {
                String n = e.name();
                String lower = n;
                lower.toLowerCase();
                if (lower.endsWith(".mp3")) // ignore the non-mp3 files
                {
                    if (g_mp3Shown < 5)
                    {
                        String disp = n;
                        if (disp.startsWith("/"))
                            disp = disp.substring(1);
                        g_mp3Names[g_mp3Shown++] = disp;
                    }
                    g_mp3Count++;
                }
            }
            e.close();
        }
        root.close();
    }

    Serial.printf("[sd] OK: %s, %.0f MB, %d mp3 file(s)\n",
                  g_sdType, g_sdSizeMB, g_mp3Count);
    for (int i = 0; i < g_mp3Shown; i++)
        Serial.printf("[sd]   - %s\n", g_mp3Names[i].c_str());
}

// Draw the SD scan result ONCE (static screen; the bus then goes idle). Uses
// the same full-buffer-once approach proven flicker-free in Step 2.
// A2DP data callback (runs on the BT task): fill `frames` with a stereo 440 Hz
// sine tone at 44.1 kHz. This is the sub-step-1 test signal proving the radio
// path; sub-step 2 will replace it with decoded MP3 PCM from the SD card.
static int32_t a2dpSineCallback(Frame *frames, int32_t frameCount)
{
    static double phase = 0.0;
    const double twoPi = 6.283185307179586;
    const double step = twoPi * 440.0 / 44100.0; // 440 Hz at 44.1 kHz
    const double amp = 6000.0;                   // well under int16 max (32767)
    for (int32_t i = 0; i < frameCount; i++)
    {
        int16_t s = (int16_t)(amp * sin(phase));
        frames[i].channel1 = s; // left
        frames[i].channel2 = s; // right
        phase += step;
        if (phase >= twoPi)
            phase -= twoPi;
    }
    return frameCount;
}

// Combined SD + Bluetooth status screen. Redrawn only on BT state change.
static void drawStatusScreen(void)
{
    u8g2.clearBuffer();
    u8g2.drawFrame(0, 0, 128, 64);

    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(5, 12, "CUE  STATUS");
    u8g2.drawHLine(2, 15, 124);

    u8g2.setFont(u8g2_font_5x7_tr);
    char line[40];

    // --- SD line ---
    if (!g_sdMounted)
        u8g2.drawStr(6, 27, "SD: MOUNT FAILED");
    else
    {
        snprintf(line, sizeof(line), "SD:%s %.0fG %dmp3",
                 g_sdType, g_sdSizeMB / 1024.0, g_mp3Count);
        u8g2.drawStr(6, 27, line);
    }

    // --- Bluetooth target + state ---
    snprintf(line, sizeof(line), "BT: %s", BT_SINK_NAME);
    u8g2.drawStr(6, 40, line);
    u8g2.drawHLine(2, 45, 124);

    u8g2.setFont(u8g2_font_6x12_tr);
    if (g_btConnected)
        u8g2.drawStr(6, 60, ">> STREAMING TONE");
    else
        u8g2.drawStr(6, 60, "pairing...");

    u8g2.sendBuffer(); // single full-buffer push
}

void setup()
{
    pinMode(LED_PIN, OUTPUT);

    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F("[boot] Cue STEP 3 — microSD bring-up (music library)"));
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

    // Mount the microSD card (dedicated SPI bus) and scan for .mp3 files.
    sdScan();

    // Configure the rotary encoders + keyboard switches.
    inputInit();

    // Start the Bluetooth A2DP source and begin connecting to the target sink.
    // Streaming runs on a BT task via a2dpSineCallback; our loop keeps running.
    // Start the Bluetooth A2DP source and begin connecting to the target sink.
    // Streaming runs on a BT task via a2dpSineCallback; our loop keeps running.
    //
    // Quiet the ESP-IDF Bluetooth stack logs. Each reconnect attempt otherwise
    // floods the console with BT_AV/BT_API/RCCT log lines. Our own [bt]/[input]
    // prints go through Serial and are unaffected. General level -> WARN, and
    // the BT tags fully silenced (they emit errors on every failed reconnect).
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("BT_AV", ESP_LOG_NONE);
    esp_log_level_set("BT_API", ESP_LOG_NONE);
    esp_log_level_set("RCCT", ESP_LOG_NONE);

    // Reconnect FAST after the speaker is turned off/on: auto-reconnect uses the
    // last device's ADDRESS (cached in NVS, survives reboots) instead of a slow
    // name-inquiry scan. Must be set BEFORE start().
    a2dp_source.set_auto_reconnect(true, 1000);
    Serial.printf("[bt] A2DP source start -> connecting to \"%s\"...\n",
                  BT_SINK_NAME);
    a2dp_source.start(BT_SINK_NAME, a2dpSineCallback);

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

    // Poll all inputs every pass (encoders need frequent sampling not to miss
    // steps); activity is printed on serial.
    inputPoll();

    // Fast Bluetooth reconnect: the A2DP library only retries on its internal
    // ~10s heartbeat timer, so after the speaker is turned off/on you'd wait up
    // to 10s. We nudge it ourselves every 500ms using the cached address
    // (reconnect() = a DIRECT connect to the saved address, NOT a scan), so Cue
    // grabs the speaker the instant it becomes reachable. Extra calls while a
    // connect is already in flight are harmless no-ops. Gated to fire ONLY after
    // a genuine drop (we were connected before) so it never disturbs the initial
    // connect or an active stream.
    {
        static uint32_t lastReconnectTry = 0;
        static bool everConnected = false;
        if (a2dp_source.is_connected())
            everConnected = true;
        else if (everConnected && (now - lastReconnectTry >= 500))
        {
            lastReconnectTry = now;
            a2dp_source.reconnect(); // immediate address-based attempt (no scan)
        }
    }

    if (g_oledPresent)
    {
        // Draw the status screen at boot, then redraw only when the Bluetooth
        // connection state changes (rare -> no flicker). Shows SD + BT status.
        bool conn = a2dp_source.is_connected();
        static int lastConn = -1;
        if ((int)conn != lastConn)
        {
            lastConn = conn;
            g_btConnected = conn;
            drawStatusScreen();
            Serial.printf("[bt] %s\n", conn ? "CONNECTED -> streaming tone"
                                            : "not connected (scanning/pairing)");
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
