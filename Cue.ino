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
#include "MP3DecoderHelix.h"
#include "freertos/stream_buffer.h"
#include "esp_heap_caps.h"
using namespace libhelix;

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
static int g_mp3Count = 0;    // total .mp3 files found
static String g_mp3Path[64];  // full path "/dir/song.mp3" for playback + ID3
static int g_mp3Shown = 0;    // how many tracks are stored

// ---- Per-track ID3 metadata -------------------------------------------------
// Only the TITLE is kept per-song (for the Songs browse list). Artist/Album/
// Genre are NOT stored per-song: those parallel String[64] arrays ate the ~8KB
// of heap the Bluetooth A2DP media stream needs to START (with them, the SBC
// encoder alloc failed and the stream never began -> silent 2% stall). The
// unique values below still power the browse lists, and the CURRENT track's
// artist/album are re-parsed on demand into g_cur* for the now-playing screen.
static String g_title[64];  // ID3 title (TIT2); empty -> fall back to filename

// Unique aggregated values for the Artists / Albums / Genre browse lists.
static String g_uArtist[64];
static int g_uArtistN = 0;
static String g_uAlbum[64];
static int g_uAlbumN = 0;
static String g_uGenre[64];
static int g_uGenreN = 0;

// Current (now-playing) track metadata, re-parsed when a track starts playing.
static String g_curTitle;
static String g_curArtist;
static String g_curAlbum;

// Now-playing state: which song is playing + decode progress (for the bar).
static volatile int g_playingIndex = 0;
static volatile uint32_t g_playPos = 0;  // bytes decoded so far this track
static volatile uint32_t g_playSize = 1; // total file bytes (1 avoids /0)
// Diagnostics: how often the A2DP consumer runs + how often it underruns.
static volatile uint32_t g_a2dpCalls = 0;
static volatile uint32_t g_a2dpUnderruns = 0;

// ---- Bluetooth A2DP source (streams audio OUT to a BT speaker/headphone) -----
// Cue is the SOURCE: it connects TO this sink by its advertised name (exact,
// case-sensitive). Sub-step 1 streams a sine tone to prove the radio path;
// sub-step 2 will replace the tone with decoded MP3 from the SD card.
#define BT_SINK_NAME "SoundCore 2"
static BluetoothA2DPSource a2dp_source;
static volatile bool g_btConnected = false;

// ---- MP3 -> PCM pipeline (SD file decoded by Helix, buffered for A2DP) --------
// A decode task reads the first .mp3 off the SD card, Helix decodes it to PCM,
// and the PCM is pushed into a FreeRTOS stream buffer. The A2DP data callback
// (BT task) pulls PCM from that buffer on demand. The buffer decouples the two
// and lets the decoder self-pace (it blocks when the buffer is full).
#define PCM_BUF_BYTES (8 * 1024)  // ~0.045s of 44.1kHz stereo PCM; kept small
                                  // so the BT stack keeps enough contiguous
                                  // heap once A2DP connects (it grabs ~90KB)
static StreamBufferHandle_t g_pcmBuf = nullptr;
static String g_firstMp3Path; // full path "/name.mp3" of first track
static volatile bool g_mp3FormatLogged = false;
// The decoder is allocated ONCE and begun EARLY (in setup, before Bluetooth),
// while heap is plentiful. Allocating it after A2DP is streaming fails (~8.7KB
// alloc) and crashes the whole system, so we reserve it up front.
static MP3DecoderHelix g_mp3;

// Press-to-play: path of the track the decode task should play. A char[] (not
// String) for safe access from both the UI (loop) task and the decode task.
static char g_playPath[256] = {0};
static volatile bool g_trackChanged = false;
// The decode task is CREATED early (in setup, while ~110KB heap is free) but
// BLOCKS on this flag until BT is connected + stable. That keeps its 8KB stack
// reserved from the roomy pre-Bluetooth heap instead of being carved out of the
// ~12KB that remains once A2DP is connected -> avoids starving the BT stack
// into an OOM crash (hash_map_set / SbcAnalysisInit asserts).
static volatile bool g_decodeGo = false;

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

// TEMP: 1 prints every raw button edge (PRESS/release) so we can confirm a
// button is electrically registering. Set back to 0 once inputs are trusted.
#define BTN_DEBUG 0

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

// ============================================================================
//  UI state — two-level menu
//  Top banner:    Music / Settings          (SW2=right, SW3=left; hides after 5s)
//  Second banner: Genre/Artists/Albums/Songs (SW1=right, SW4=left; persists)
//  Encoder 1 scrolls the list (fwd=down, back=up); '>' marks the selection.
//  Genre/Artists/Albums are PLACEHOLDER lists until ID3 metadata parsing is
//  added; "Songs" shows the real .mp3 filenames from the SD card.
// ============================================================================
static const char *TOP_ITEMS[] = {"Playing", "Library", "Settings"};
static const int TOP_N = 3;
static const char *CAT_ITEMS[] = {"Genre", "Artists", "Albums", "Songs"};
static const int CAT_N = 4;

static int g_topIndex = 1;    // view: 0=Playing, 1=Library, 2=Settings
static int g_catIndex = 3;    // default to Songs so the real library shows
static int g_listIndex = 0;   // selected row in the current list
static int g_listTop = 0;     // first visible row (viewport scroll)
static bool g_uiDirty = true; // set true to request a redraw

// Basename (portion after the last '/') of a full path.
static String baseName(const String &path)
{
    int slash = path.lastIndexOf('/');
    return (slash >= 0) ? path.substring(slash + 1) : path;
}

// Item count of the currently selected category's list.
static int currentListCount(void)
{
    switch (g_catIndex)
    {
    case 0:
        return g_uGenreN; // Genre
    case 1:
        return g_uArtistN; // Artists
    case 2:
        return g_uAlbumN; // Albums
    default:
        return g_mp3Shown; // Songs
    }
}

// Text of item `i` in the currently selected category's list.
static String currentListItem(int i)
{
    switch (g_catIndex)
    {
    case 0:
        return g_uGenre[i];
    case 1:
        return g_uArtist[i];
    case 2:
        return g_uAlbum[i];
    default:
        return g_title[i].length() ? g_title[i] : baseName(g_mp3Path[i]);
    }
}

// One-button view switch: cycles Playing -> Library -> Settings -> Playing on
// EVERY press. There is no on-screen menu bar; the body content itself shows
// which view you're in.
static void uiTopMove(int delta)
{
    g_topIndex = (g_topIndex + delta + TOP_N) % TOP_N;
    g_uiDirty = true;
    Serial.printf("[ui] view -> %s (idx %d)\n", TOP_ITEMS[g_topIndex], g_topIndex);
}

// Second banner: switch category and reset the list to the top.
static void uiCatMove(int delta)
{
    int ni = constrain(g_catIndex + delta, 0, CAT_N - 1);
    if (ni != g_catIndex)
    {
        g_catIndex = ni;
        g_listIndex = 0;
        g_listTop = 0;
        g_uiDirty = true;
    }
}

// Encoder-1 list scroll: +1 = down, -1 = up.
static void uiListScroll(int delta)
{
    int count = currentListCount();
    if (count <= 0)
        return;
    int ni = constrain(g_listIndex + delta, 0, count - 1);
    if (ni != g_listIndex)
    {
        g_listIndex = ni;
        g_uiDirty = true;
    }
}

// Encoder-1 push: play the currently selected song (only in the Songs list),
// remember it as the now-playing track, and jump to the Playing view.
static void uiPlaySelected(void)
{
    if (g_catIndex == 3 && g_mp3Shown > 0)
    {
        g_playingIndex = g_listIndex;
        strncpy(g_playPath, g_mp3Path[g_listIndex].c_str(), sizeof(g_playPath) - 1);
        g_playPath[sizeof(g_playPath) - 1] = 0;
        g_trackChanged = true;
        loadCurrentMeta(g_listIndex); // refresh now-playing title/artist/album
        g_topIndex = 0; // jump to the now-playing screen
        g_uiDirty = true;
        Serial.printf("[mp3] play request: %s\n", g_playPath);
    }
}

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
#if BTN_DEBUG
        Serial.printf("[btn-dbg] %s (GPIO%u) %s\n", b.name, b.pin,
                      raw ? "release" : "PRESS");
#endif
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

// Poll every input, drive the UI, and log activity on serial. Runs every pass.
static void inputPoll(void)
{
    int8_t d1 = encoderPoll(g_enc1);
    if (d1)
    {
        g_enc1Pos += d1;
        uiListScroll(d1); // encoder 1: forward = down, back = up
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
        {
            const char *nm = g_buttons[i].name;
            Serial.printf("[input] %s pressed\n", nm);
            // SW3 is the sole top-menu control: it cycles Playing -> Library
            // -> Settings -> Playing. (SW2 is now unused / free for later.)
            if (!strcmp(nm, "SW3"))
                uiTopMove(+1);
            else if (!strcmp(nm, "SW1"))
                uiCatMove(+1); // category right
            else if (!strcmp(nm, "SW4"))
                uiCatMove(-1); // category left
            else if (!strcmp(nm, "ENC1_SW"))
                uiPlaySelected(); // encoder-1 push = play selected song
        }
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

// Recursively collect .mp3 files (with full paths) from `dir`. `prefix` is the
// directory path with a trailing '/'. Descends into subfolders so a normal
// music library (Artist/Album/song.mp3) works. Caps at 64 tracks; skips hidden
// and AppleDouble ("._") entries.
static void sdScanDir(File dir, const String &prefix)
{
    while (g_mp3Shown < 64)
    {
        File e = dir.openNextFile();
        if (!e)
            break;

        // Basename, regardless of whether name() returns base or a full path.
        String nm = e.name();
        int slash = nm.lastIndexOf('/');
        String base = (slash >= 0) ? nm.substring(slash + 1) : nm;

        if (base.length() == 0 || base.startsWith(".")) // hidden / "._" junk
        {
            e.close();
            continue;
        }

        if (e.isDirectory())
        {
            sdScanDir(e, prefix + base + "/");
        }
        else
        {
            String lower = base;
            lower.toLowerCase();
            if (lower.endsWith(".mp3"))
            {
                g_mp3Path[g_mp3Shown] = prefix + base;
                g_mp3Shown++;
                g_mp3Count++;
            }
        }
        e.close();
    }
}

// Mount the microSD card on the dedicated SPI bus, then recursively scan for
// .mp3 files (including inside folders) and gather a summary.
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
        sdScanDir(root, "/"); // recurse into folders too
        root.close();
    }
    if (g_mp3Shown > 0)
        g_firstMp3Path = g_mp3Path[0];

    Serial.printf("[sd] OK: %s, %.0f MB, %d mp3 file(s)\n",
                  g_sdType, g_sdSizeMB, g_mp3Count);
    for (int i = 0; i < g_mp3Shown; i++)
        Serial.printf("[sd]   - %s\n", g_mp3Path[i].c_str());
}

// ---- ID3 tag parsing --------------------------------------------------------
// Minimal ID3v2 (v2.2/2.3/2.4) reader: pulls title/artist/album/genre. Large
// frames (e.g. album art) are skipped via seek, so it stays fast.

// Add a value to a unique-string list (skips empties + duplicates).
static void addUnique(String *arr, int &n, const String &v)
{
    if (v.length() == 0 || n >= 64)
        return;
    for (int i = 0; i < n; i++)
        if (arr[i] == v)
            return;
    arr[n++] = v;
}

// 4-byte syncsafe integer (7 bits per byte) used by ID3v2 sizes.
static uint32_t id3SyncSafe(const uint8_t *p)
{
    return ((uint32_t)(p[0] & 0x7f) << 21) | ((uint32_t)(p[1] & 0x7f) << 14) |
           ((uint32_t)(p[2] & 0x7f) << 7) | (uint32_t)(p[3] & 0x7f);
}

// Read a text frame body of `size` bytes at the current position, decoding the
// leading encoding byte (0=Latin1, 1=UTF-16+BOM, 2=UTF-16BE, 3=UTF-8). Returns
// best-effort ASCII.
static String id3ReadText(File &f, uint32_t size)
{
    if (size == 0)
        return String();
    int enc = f.read();
    int remaining = (int)size - 1;
    String s;
    if (enc == 1)
    {
        int b0 = f.read();
        int b1 = f.read();
        remaining -= 2;
        bool le = !(b0 == 0xFE && b1 == 0xFF); // BE only if BOM says so
        while (remaining >= 2)
        {
            int x = f.read();
            int y = f.read();
            remaining -= 2;
            int c = le ? x : y;
            if (c >= 32 && c < 127)
                s += (char)c;
        }
    }
    else if (enc == 2)
    {
        while (remaining >= 2)
        {
            f.read();         // high byte (0 for ASCII)
            int c = f.read(); // low byte
            remaining -= 2;
            if (c >= 32 && c < 127)
                s += (char)c;
        }
    }
    else // 0 (Latin-1) or 3 (UTF-8)
    {
        for (int i = 0; i < remaining; i++)
        {
            int c = f.read();
            if (c >= 32)
                s += (char)c;
        }
    }
    s.trim();
    return s;
}

// Parse ID3v2 text frames of `path` into the provided output strings. A frame
// that is absent is left untouched, so callers should pre-clear the outputs.
static void parseID3(const char *path, String &title, String &artist,
                     String &album, String &genre)
{
    File f = SD.open(path, FILE_READ);
    if (!f)
        return;
    uint8_t hdr[10];
    if (f.read(hdr, 10) == 10 && memcmp(hdr, "ID3", 3) == 0)
    {
        uint8_t major = hdr[3];
        uint32_t tagSize = id3SyncSafe(hdr + 6);
        bool v22 = (major == 2);
        uint32_t frameHdrLen = v22 ? 6 : 10;
        uint32_t pos = 10;             // first frame
        uint32_t end = 10 + tagSize;   // end of the tag
        while (pos + frameHdrLen <= end)
        {
            f.seek(pos);
            uint8_t fh[10];
            if ((uint32_t)f.read(fh, frameHdrLen) != frameHdrLen)
                break;
            char id[5] = {0};
            uint32_t fsize;
            if (v22)
            {
                memcpy(id, fh, 3);
                fsize = ((uint32_t)fh[3] << 16) | ((uint32_t)fh[4] << 8) | fh[5];
            }
            else
            {
                memcpy(id, fh, 4);
                if (major == 4)
                    fsize = id3SyncSafe(fh + 4);
                else
                    fsize = ((uint32_t)fh[4] << 24) | ((uint32_t)fh[5] << 16) |
                            ((uint32_t)fh[6] << 8) | fh[7];
            }
            if (id[0] < 'A' || id[0] > 'Z' || fsize == 0 || fsize > end)
                break; // padding or garbage -> stop
            uint32_t content = pos + frameHdrLen;
            String *dst = nullptr;
            if (v22)
            {
                if (!strcmp(id, "TT2")) dst = &title;
                else if (!strcmp(id, "TP1")) dst = &artist;
                else if (!strcmp(id, "TAL")) dst = &album;
                else if (!strcmp(id, "TCO")) dst = &genre;
            }
            else
            {
                if (!strcmp(id, "TIT2")) dst = &title;
                else if (!strcmp(id, "TPE1")) dst = &artist;
                else if (!strcmp(id, "TALB")) dst = &album;
                else if (!strcmp(id, "TCON")) dst = &genre;
            }
            if (dst)
            {
                f.seek(content);
                *dst = id3ReadText(f, fsize);
            }
            pos = content + fsize;
        }
    }
    f.close();
}

// Re-parse the CURRENT track's tags into g_cur* (called on track select + boot).
// Runs in loop/UI context (same thread as drawNowPlaying) so no cross-task race.
static void loadCurrentMeta(int idx)
{
    g_curTitle = "";
    g_curArtist = "";
    g_curAlbum = "";
    if (idx < 0 || idx >= g_mp3Shown)
        return;
    String genre;
    parseID3(g_mp3Path[idx].c_str(), g_curTitle, g_curArtist, g_curAlbum, genre);
    if (!g_curTitle.length())
        g_curTitle = g_title[idx].length() ? g_title[idx] : baseName(g_mp3Path[idx]);
}

// Parse every song's tags: keep the title per-song and build the unique
// Artist/Album/Genre browse lists (artist/album/genre are NOT kept per-song).
static void scanMetadata(void)
{
    Serial.println(F("[meta] parsing ID3 tags..."));
    for (int i = 0; i < g_mp3Shown; i++)
    {
        String title, artist, album, genre;
        parseID3(g_mp3Path[i].c_str(), title, artist, album, genre);
        g_title[i] = title;
        addUnique(g_uArtist, g_uArtistN, artist);
        addUnique(g_uAlbum, g_uAlbumN, album);
        addUnique(g_uGenre, g_uGenreN, genre);
    }
    if (g_mp3Shown > 0)
        loadCurrentMeta(0); // prime now-playing for the default track
    Serial.printf("[meta] %d songs, %d artists, %d albums, %d genres\n",
                  g_mp3Shown, g_uArtistN, g_uAlbumN, g_uGenreN);
}

// PCM callback from the Helix MP3 decoder (runs on the decode task): push the
// decoded interleaved stereo samples into the ring buffer. Blocks when the
// buffer is full, which self-paces the decoder to the A2DP consumption rate.
static void mp3PcmCallback(MP3FrameInfo &info, short *pcm, size_t len, void *ref)
{
    if (!g_mp3FormatLogged)
    {
        g_mp3FormatLogged = true;
        Serial.printf("[mp3] format: %d Hz, %d ch, %d-bit\n",
                      info.samprate, info.nChans, info.bitsPerSample);
        if (info.samprate != 44100 || info.nChans != 2)
            Serial.println(F("[mp3] WARNING: expected 44.1kHz stereo; audio may "
                             "play at the wrong pitch/speed."));
    }
    if (g_pcmBuf)
        xStreamBufferSend(g_pcmBuf, pcm, len * sizeof(short), portMAX_DELAY);
}

// A2DP data callback (runs on the BT task): pull decoded PCM from the ring
// buffer into the frame buffer. On underrun (buffer empty) output silence so
// the stream never stalls.
static int32_t a2dpPcmCallback(Frame *frames, int32_t frameCount)
{
    if (frameCount <= 0)
        return 0; // defensive: never compute a bogus (huge) memset length
    g_a2dpCalls++;
    size_t wanted = (size_t)frameCount * sizeof(Frame); // Frame = L+R int16 = 4B
    size_t got = 0;
    if (g_pcmBuf)
        got = xStreamBufferReceive(g_pcmBuf, frames, wanted, 0); // non-blocking
    if (got < wanted)
    {
        g_a2dpUnderruns++;
        memset((uint8_t *)frames + got, 0, wanted - got); // silence on underrun
    }
    return frameCount;
}

// Decode task: play the requested track (g_playPath). On end-of-track it loops;
// on a play request (g_trackChanged, set by encoder-1 push) it switches to the
// newly selected song. Uses the global g_mp3, already begun in setup().
static void mp3DecodeTask(void *param)
{
    const size_t CHUNK = 1024;
    uint8_t inbuf[CHUNK];

    // Block until loop() confirms the A2DP link is up + stable. The task exists
    // (stack already allocated from the plentiful pre-BT heap) but must not
    // decode or push PCM until the consumer exists AND the BT stack has
    // finished its own start-up allocations, or we starve it into an OOM crash.
    while (!g_decodeGo)
        vTaskDelay(pdMS_TO_TICKS(50));

    for (;;)
    {
        // Snapshot the requested path (char[] -> safe across tasks).
        char path[256];
        strncpy(path, g_playPath, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
        g_trackChanged = false;

        File f = SD.open(path, FILE_READ);
        if (!f)
        {
            Serial.printf("[mp3] cannot open %s\n", path);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        Serial.printf("[mp3] playing %s (%u bytes)\n", path, (unsigned)f.size());
        g_mp3FormatLogged = false; // re-log the format for the new track
        g_playSize = f.size() ? f.size() : 1;
        g_playPos = 0;

        // Stream the file, but bail out early if a new track was requested.
        while (f.available() && !g_trackChanged)
        {
            int n = f.read(inbuf, CHUNK);
            if (n > 0)
            {
                g_mp3.write(inbuf, n); // -> mp3PcmCallback -> ring buffer
                g_playPos += n;
            }
        }
        f.close();

        if (g_trackChanged)
        {
            // Do NOT xStreamBufferReset() here: the BT (consumer) task may be
            // reading this buffer concurrently on the other core, and a reset
            // racing a receive corrupts the buffer's internal state -> heap
            // corruption -> random BT-stack asserts (SbcAnalysisInit /
            // hash_map_set). The ~90ms of already-buffered audio (16KB) simply
            // plays out before the new track begins; that brief tail is
            // imperceptible and, crucially, race-free.
            Serial.println(F("[mp3] switching track"));
        }
        else
        {
            Serial.println(F("[mp3] end of track -> looping"));
        }
    }
}

// --- Library view: category tabs (persistent) + scrollable list ---
static void drawLibrary(int16_t y)
{
    u8g2.setFont(u8g2_font_5x7_tr);
    {
        int16_t x = 1;
        for (int i = 0; i < CAT_N; i++)
        {
            int16_t w = u8g2.getStrWidth(CAT_ITEMS[i]) + 2;
            if (i == g_catIndex)
            {
                u8g2.drawBox(x, y, w, 9);
                u8g2.setDrawColor(0);
                u8g2.drawStr(x + 1, y + 7, CAT_ITEMS[i]);
                u8g2.setDrawColor(1);
            }
            else
                u8g2.drawStr(x + 1, y + 7, CAT_ITEMS[i]);
            x += w + 1;
        }
    }
    y += 10;
    u8g2.drawHLine(0, y, 128);
    y += 2;

    int count = currentListCount();
    const int rowH = 10;
    u8g2.setFont(u8g2_font_6x10_tr);
    int16_t listTop = y + 1;
    int visRows = (64 - listTop) / rowH;
    if (visRows < 1)
        visRows = 1;

    // keep the selected row inside the viewport
    if (g_listIndex < g_listTop)
        g_listTop = g_listIndex;
    else if (g_listIndex >= g_listTop + visRows)
        g_listTop = g_listIndex - visRows + 1;

    if (count <= 0)
        u8g2.drawStr(8, listTop + 8, "(empty)");
    else
        for (int r = 0; r < visRows; r++)
        {
            int idx = g_listTop + r;
            if (idx >= count)
                break;
            int16_t ry = listTop + r * rowH + 8;
            if (idx == g_listIndex)
                u8g2.drawStr(0, ry, ">");
            String item = currentListItem(idx);
            if (item.length() > 19)
                item = item.substring(0, 19);
            u8g2.drawStr(8, ry, item.c_str());
        }
}

// --- Now-playing view: title, artist - album, progress bar, status ---
static void drawNowPlaying(int16_t y)
{
    int16_t top = y;

    String title = "(no track)";
    String artist = "Unknown Artist";
    String album = "Unknown Album";
    if (g_mp3Shown > 0)
    {
        if (g_curTitle.length())
            title = g_curTitle;
        if (g_curArtist.length())
            artist = g_curArtist;
        if (g_curAlbum.length())
            album = g_curAlbum;
    }

    u8g2.setFont(u8g2_font_6x10_tr);
    if (title.length() > 20)
        title = title.substring(0, 20);
    u8g2.drawStr(2, top + 11, title.c_str());

    u8g2.setFont(u8g2_font_5x7_tr);
    String aa = artist + " - " + album;
    if (aa.length() > 24)
        aa = aa.substring(0, 24);
    u8g2.drawStr(2, top + 22, aa.c_str());

    // Progress bar (decode position through the file).
    int16_t by = top + 27;
    u8g2.drawFrame(2, by, 124, 7);
    float frac = (float)g_playPos / (float)g_playSize;
    if (frac < 0)
        frac = 0;
    if (frac > 1)
        frac = 1;
    int16_t fw = (int16_t)(120 * frac);
    if (fw > 0)
        u8g2.drawBox(4, by + 2, fw, 3);

    // Status line: percent + BT connection state.
    char st[32];
    snprintf(st, sizeof(st), "%d%%   %s", (int)(frac * 100),
             g_btConnected ? "BT ok" : "BT ...");
    u8g2.drawStr(2, top + 45, st);
}

// --- Settings view: placeholder ---
static void drawSettings(int16_t y)
{
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(4, y + 14, "Settings");
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(4, y + 28, "(coming soon)");
    u8g2.drawStr(4, y + 40, "Sink: " BT_SINK_NAME);
}

// Render the whole UI. Full-buffer redraw, called only when state changes
// (input / BT / banner hide / now-playing progress) -> no per-frame flicker.
static void drawUI(void)
{
    u8g2.clearBuffer();

    // No menu bar: the selected view uses the whole screen. The body content
    // (now-playing / category tabs / settings) tells you which view you're in.
    if (g_topIndex == 0)
        drawNowPlaying(0);
    else if (g_topIndex == 2)
        drawSettings(0);
    else
        drawLibrary(0);

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

    // Parse ID3 tags to populate song titles + the Artist/Album/Genre lists.
    scanMetadata();

    // Configure the rotary encoders + keyboard switches.
    inputInit();

    // Build the MP3 -> PCM pipeline ring buffer, and allocate the Helix decoder
    // NOW while heap is plentiful (before Bluetooth). If we allocate it later
    // (after A2DP is streaming) the ~8.7KB alloc fails and crashes the system.
    g_pcmBuf = xStreamBufferCreate(PCM_BUF_BYTES, 1);
    if (g_sdMounted && g_mp3Count > 0)
    {
        // Default to the first track until the user presses encoder-1 to play
        // a selected song.
        strncpy(g_playPath, g_firstMp3Path.c_str(), sizeof(g_playPath) - 1);
        g_mp3.setDataCallback(mp3PcmCallback);
        if (g_mp3.begin())
            Serial.printf("[mp3] decoder ready, free heap now %d\n",
                          ESP.getFreeHeap());
        else
            Serial.println(F("[mp3] decoder begin FAILED (out of memory)"));

        // Create the decode task NOW, while heap is plentiful (~110KB). It
        // blocks on g_decodeGo until loop() sees a stable BT link, so its 8KB
        // stack comes from the roomy pre-Bluetooth heap rather than the ~12KB
        // left once A2DP is connected (carving it out post-connect starved the
        // BT stack -> hash_map_set / SbcAnalysisInit OOM asserts).
        xTaskCreatePinnedToCore(mp3DecodeTask, "mp3dec", 8192, nullptr, 5,
                                nullptr, 1);
    }
    else
        Serial.println(F("[mp3] no track to play (card/mp3 missing) -> silence"));

    // Quiet the ESP-IDF Bluetooth stack logs (each reconnect attempt otherwise
    // floods the console). Our own [bt]/[input] prints go through Serial and are
    // unaffected. General level -> WARN, BT tags fully silenced.
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("BT_AV", ESP_LOG_NONE);
    esp_log_level_set("BT_API", ESP_LOG_NONE);
    esp_log_level_set("RCCT", ESP_LOG_NONE);

    // Start the Bluetooth A2DP source: connect to the sink and stream the
    // decoded MP3 PCM (a2dpPcmCallback pulls from the ring buffer). Reconnect
    // FAST after off/on via the cached address (set BEFORE start()).
    a2dp_source.set_auto_reconnect(true, 1000);
    Serial.printf("[bt] A2DP source start -> connecting to \"%s\"...\n",
                  BT_SINK_NAME);
    a2dp_source.start(BT_SINK_NAME, a2dpPcmCallback);

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

    // Launch the MP3 decode task on the FIRST successful BT connection. Decoding
    // before a consumer exists is pointless, and it keeps heavy decode load away
    // from the power-sensitive BT connect phase (where the reset loop appeared).
    {
        static bool decodeReleased = false;
        static uint32_t connectedSince = 0;
        bool btUp = a2dp_source.is_connected();
        if (!btUp)
            connectedSince = 0;
        else if (connectedSince == 0)
            connectedSince = now;
        // Release the (already-created, blocked) decode task once the A2DP link
        // has been up ~800ms, letting the BT stack finish its own start-up
        // allocations first. We only flip a flag here -> NO late heap allocation
        // that would fragment the little heap the BT stack left us.
        if (!decodeReleased && btUp && connectedSince &&
            (now - connectedSince >= 800) &&
            g_pcmBuf && g_sdMounted && g_mp3Count > 0)
        {
            decodeReleased = true;
            g_decodeGo = true;
            Serial.printf("[mp3] BT stable -> decode go "
                          "(free heap %u, largest block %u)\n",
                          (unsigned)ESP.getFreeHeap(),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
        }
    }

    // 1Hz pipeline diagnostic: shows whether the decode task is feeding (pos
    // climbing, buf > 0) and whether the BT consumer is pulling (a2dp delta > 0)
    // vs underrunning (under delta high). Frozen pos + full buf + a2dp=0 => BT
    // stopped pulling; frozen pos + empty buf => decode task hung/died.
    {
        static uint32_t lastDbg = 0;
        static uint32_t lastCalls = 0, lastUnder = 0;
        if (g_decodeGo && (now - lastDbg >= 1000))
        {
            lastDbg = now;
            uint32_t calls = g_a2dpCalls, under = g_a2dpUnderruns;
            unsigned bufAvail = g_pcmBuf ? (unsigned)xStreamBufferBytesAvailable(g_pcmBuf) : 0;
            int pct = g_playSize ? (int)((uint64_t)g_playPos * 100 / g_playSize) : 0;
            Serial.printf("[dbg] pos=%u/%u (%d%%) buf=%u/%u a2dp=+%u under=+%u conn=%d\n",
                          (unsigned)g_playPos, (unsigned)g_playSize, pct,
                          bufAvail, (unsigned)PCM_BUF_BYTES,
                          (unsigned)(calls - lastCalls), (unsigned)(under - lastUnder),
                          a2dp_source.is_connected() ? 1 : 0);
            lastCalls = calls;
            lastUnder = under;
        }
    }

    if (g_oledPresent)
    {
        // Reflect Bluetooth connection changes into the UI (and log them).
        bool conn = a2dp_source.is_connected();
        static int lastConn = -1;
        if ((int)conn != lastConn)
        {
            lastConn = conn;
            g_btConnected = conn;
            g_uiDirty = true;
            Serial.printf("[bt] %s\n", conn ? "CONNECTED -> streaming mp3"
                                            : "not connected (scanning/pairing)");
        }

        // While the now-playing view is up, refresh ~1x/sec so the progress
        // bar advances (slow enough that the redraw stays flicker-free).
        static uint32_t lastNpRefresh = 0;
        if (g_topIndex == 0 && (now - lastNpRefresh >= 1000))
        {
            lastNpRefresh = now;
            g_uiDirty = true;
        }

        // Redraw only when the UI state actually changed (input/BT/hide).
        if (g_uiDirty)
        {
            g_uiDirty = false;
            drawUI();
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
