/*
 * ============================================================================
 *  cue-new  —  BARE-BOARD proof-of-life for Cue (Arduino)
 * ============================================================================
 *  Board : ESP32 DevKitV1 (ESP32-WROOM-32, classic dual-core, 4MB flash)
 *
 *  PURPOSE
 *  -------
 *  Prove the brand-new ESP32 boots and talks over USB with NOTHING else
 *  attached. This is step 1 of a deliberately incremental bring-up:
 *
 *      STEP 1 (this sketch): board + USB cable ONLY. No OLED, no resistors,
 *                            no jumpers. Confirm serial heartbeat + LED blink.
 *      STEP 2 (later):       add the OLED and see how far it gets bare.
 *      STEP 3 (later):       add pull-ups / other parts ONLY if deemed needed.
 *
 *  HARDWARE REQUIRED
 *  -----------------
 *      - ESP32 DevKitV1
 *      - one USB cable to the computer
 *      That's it. Nothing wired to any GPIO.
 *
 *  WHAT YOU SHOULD SEE
 *  -------------------
 *   - The on-board blue LED (GPIO2) blinks ~1 Hz.
 *   - Serial @115200 prints a boot banner, chip info, then a "[alive] tick N"
 *     heartbeat once per second. Watch it with:  make monitor
 * ============================================================================
 */

#include <Arduino.h>

// On-board LED on most ESP32 DevKitV1 boards is wired to GPIO2.
#define LED_PIN 2

void setup()
{
    pinMode(LED_PIN, OUTPUT);

    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F("[boot] Cue bare-board proof-of-life"));
    Serial.println(F("[boot] ESP32 DevKitV1 — USB only, no peripherals"));
    Serial.printf("[boot] chip: %s rev %d, %d core(s)\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
    Serial.printf("[boot] CPU: %d MHz   flash: %d bytes\n",
                  ESP.getCpuFreqMHz(), ESP.getFlashChipSize());
    Serial.printf("[boot] free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println(F("[boot] setup done — starting heartbeat"));
    Serial.println(F("========================================"));
}

void loop()
{
    static uint32_t tick = 0;

    // Blink so the board is visibly alive even without the serial monitor open.
    digitalWrite(LED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_PIN, LOW);
    delay(500);

    Serial.printf("[alive] tick %lu   heap=%d\n",
                  (unsigned long)++tick, ESP.getFreeHeap());
}
