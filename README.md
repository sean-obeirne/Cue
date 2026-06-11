# Cue

A purpose-built portable music player focused on albums, physical
controls, and distraction-free listening. Hardware: ESP32-S3-N16R8
(YD-ESP32-S3) with a 2.42" 128×64 SSD1309 OLED, rotary encoder,
four iPod-style buttons, microSD, and (eventually) I²S audio out
plus Bluetooth A2DP.

## Lineage

Large chunks of the firmware are ported from
[`macro-blues`](../../references/macro-blues): the debounce filter,
the rotary encoder gray-code state machine, the framebuffer + 5×7
glyph font, the SSD1306/SSD1309 init sequence, the LiPo
voltage-to-percent curve, and the three-phase init / 10 ms
scan-loop structure of `main()`. Pin definitions and any module
that talked to the nRF52 SoftDevice (BLE HID, bond, etc.) were
intentionally dropped.

## Layout

```
main/                application entry, glue
components/
  board/             pin map + thresholds (header only)
  display/           SSD1309 driver + framebuffer + glyphs
  input/             buttons.c, encoder.c, debounce.c
  battery/           ADC1 oneshot LiPo monitor
  storage/           microSD mount (stub)
  audio/             I²S playback engine (stub)
  bt_audio/          A2DP placeholder
  ui/                iPod-flavoured menu + now-playing screens
```

## Pin map (YD-ESP32-S3)

| Function           | GPIO              | Notes                              |
|--------------------|-------------------|------------------------------------|
| I²C SDA / SCL      | 8 / 9             | OLED (`0x3C`) + future sensors     |
| Rotary A / B / SW  | 4 / 5 / 6         | RTC GPIO (wake-capable)            |
| Btn Menu / ⏮ / ⏭ / ⏯ | 10 / 11 / 12 / 13 | active-low, internal pull-ups      |
| SD MOSI/MISO/SCK/CS| 38 / 40 / 42 / 21 | SPI2 host                          |
| I²S BCK / LRCK / DOUT | 47 / 45 / 46  | reserved                           |
| VBAT sense         | 1 (ADC1_CH0)      | external 2:1 divider               |
| Status RGB         | 48                | only when on-board RGB pad bridged |

Strapping pins (0, 45, 46) are touched only where unavoidable
and never with strong pull-ups at boot. GPIO 35/36/37 are skipped
because the N16R8 module uses them for octal flash / PSRAM.

## Build

Everything goes through `make`, which wraps `idf.py`.  The Makefile
uses `bash` internally for ESP-IDF's `export.sh`, so it works the
same regardless of your interactive shell (fish, zsh, …).  Run
`make help` for the full list of targets.

```sh
# One-time host setup on Fedora.
make deps                 # dnf install build prereqs
make install              # clone ESP-IDF v5.3.1 → ~/esp/esp-idf, run install.sh
sudo usermod -aG dialout $USER   # serial port access — log out / back in after

# Compile.
make build

# Flash + open the serial monitor.  Override PORT=/dev/ttyXXX if needed.
make run            # = flash + monitor
make flash          # just flash
make monitor        # just monitor (Ctrl+] to quit)
```

If you'd rather drive `idf.py` directly from a fish prompt, source
the fish-flavoured env script ESP-IDF ships:

```fish
source ~/esp/esp-idf/export.fish
idf.py build
```

Other useful targets: `make menuconfig`, `make size`, `make erase`,
`make clean`, `make fullclean`.

If ESP-IDF is already installed elsewhere, point the Makefile at it:
`make build IDF_PATH=/opt/esp/esp-idf`.

`partitions.csv` reserves a 4 MB app slot and an 8 MB FAT partition
for on-flash data; SD-card content mounts at `/sdcard`.

## Status

Compiles to a working shell:

- ✅ display, buttons, encoder, debounce, battery — full ports
- ⏳ storage, audio, bt_audio — typed stubs only
- ⏳ A2DP source, on-disk library scan, decoder selection — TODO
