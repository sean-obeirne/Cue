# ============================================================================
#  Makefile — build / upload / monitor for the cue-new OLED bring-up spike
#  Target board: ESP32-S3 N16R8 (16MB flash / 8MB OPI PSRAM)
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
# CDCOnBoot=default routes Serial to UART0 (the CH343 COM port = ttyACM0) so the
# [oled] I2C scan prints on the same port `make monitor` watches. Use =cdc to
# send Serial to the native-USB port instead.
FQBN       := esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=default
PORT       ?= /dev/ttyACM0
BAUD       ?= 115200
CLI        := arduino-cli

# --- Direct-flash settings (esptool v4) -------------------------------------
ESPTOOL          ?= esptool
CHIP             := esp32s3
FLASH_BAUD       ?= 460800
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
	$(CLI) monitor -p "$(PORT)" -c baudrate=$(BAUD)

.PHONY: flash
flash: upload monitor

.PHONY: ports
ports:
	$(CLI) board list

.PHONY: port-check
port-check:
	@if [ ! -e "$(PORT)" ]; then \
		echo "!! Port $(PORT) not found."; \
		echo "   Plug the board's NATIVE USB port, or: make upload PORT=/dev/ttyACMx"; \
		echo "   Connect retries forever: just HOLD BOOT until 'Writing at...'."; \
		echo "   Or enter download mode by hand: make upload BEFORE=no_reset"; \
		exit 1; \
	fi
