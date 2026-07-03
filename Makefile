# ============================================================================
#  Makefile — build / upload / monitor for the cue-new OLED bring-up spike
#  Target board: ESP32 DevKitV1 (ESP32-WROOM-32, 4MB flash, no PSRAM)
#  Mirrors ../not-cue/Makefile so the workflow is identical.
# ============================================================================
#  Common usage:
#     make            # compile + upload + monitor (default)
#     make build      # compile only (no board needed)
#     make upload     # compile + flash to the board
#     make monitor    # open the serial monitor (watch the I2C probe + frames)
#     make deps       # install the U8g2 library via arduino-cli
#     make ports      # list connected boards / serial ports
#
#  Override the port if it is not /dev/ttyACM0:
#     make upload PORT=/dev/ttyACM1
#
#  Upload uses esptool directly against the merged image so we control the
#  reset/retry behaviour (arduino-cli's black-box upload could not).
#  CONNECT_ATTEMPTS=0 means esptool retries the connect handshake FOREVER, so
#  on a marginal 3V3 rail you can just HOLD BOOT until it catches.
#  Overrides:
#     make upload BEFORE=no_reset        # don't pulse RTS/DTR; enter download
#                                        # mode by hand (hold BOOT, tap RST)
#     make upload FLASH_BAUD=115200      # slower, more tolerant of noisy lines
# ============================================================================

SKETCH_DIR := $(CURDIR)
BUILD_DIR  := $(SKETCH_DIR)/build
# Classic ESP32 DevKitV1 (ESP32-WROOM-32). Serial goes to UART0 via the on-board
# CP2102/CH340 = the USB COM port `make monitor` watches; no CDCOnBoot on classic.
FQBN       := esp32:esp32:esp32doit-devkit-v1
PORT       ?= /dev/ttyUSB0
BAUD       ?= 115200
CLI        := arduino-cli

# --- Direct-flash settings (esptool v4) -------------------------------------
ESPTOOL          ?= esptool
CHIP             := esp32
# 115200: this board's USB-serial adapter fails at 460800 ("No serial data
# received" right after the baud switch, + garbled crystal-freq detection).
# 115200 flashes reliably (~34s for 4MB). Bump only if you swap to a better cable.
FLASH_BAUD       ?= 115200
BEFORE           ?= default_reset
AFTER            ?= hard_reset
CONNECT_ATTEMPTS ?= 0
MERGED           := $(BUILD_DIR)/Cue.ino.merged.bin

.NOTPARALLEL:

.PHONY: all
all: flash

# --- Install required libraries ---------------------------------------------
.PHONY: deps
deps:
	@echo ">> Installing U8g2 library"
	$(CLI) lib install "U8g2"

.PHONY: build
build:
	@echo ">> Compiling ($(FQBN))"
	$(CLI) compile --fqbn "$(FQBN)" --output-dir "$(BUILD_DIR)" "$(SKETCH_DIR)"

.PHONY: upload
upload: build port-check
	@echo ">> Flashing $(MERGED)"
	@echo ">>   port=$(PORT) baud=$(FLASH_BAUD) before=$(BEFORE) attempts=$(CONNECT_ATTEMPTS)"
	@echo ">>   Rail browns out? HOLD BOOT now and keep holding until 'Writing at...'."
	$(ESPTOOL) --chip $(CHIP) --port "$(PORT)" --baud $(FLASH_BAUD) \
		--before $(BEFORE) --after $(AFTER) --connect-attempts $(CONNECT_ATTEMPTS) \
		write_flash --flash_mode keep --flash_freq keep --flash_size keep \
		-z 0x0 "$(MERGED)"
	@echo ">> Done. Tap RESET if the sketch does not start automatically."

.PHONY: monitor
monitor: port-check
	@echo ">> Monitor on $(PORT) @ $(BAUD)  (Ctrl-C to exit)"
	$(CLI) monitor -p "$(PORT)" -c baudrate=$(BAUD),dtr=off,rts=off

.PHONY: flash
flash: upload monitor

.PHONY: ports
ports:
	$(CLI) board list

.PHONY: port-check
port-check:
	@if [ ! -e "$(PORT)" ]; then \
		echo "!! Port $(PORT) not found."; \
		echo "   Classic ESP32 DevKitV1 enumerates as /dev/ttyUSB0 (CP2102/CH340)."; \
		echo "   List ports: make ports  (or: make upload PORT=/dev/ttyUSBx)"; \
		echo "   Connect retries forever: just HOLD BOOT until 'Writing at...'."; \
		echo "   Or enter download mode by hand: make upload BEFORE=no_reset"; \
		exit 1; \
	fi
