# Cue — convenience wrapper around ESP-IDF's idf.py
#
# Quick start (fresh machine):
#   make install      # one-time: clone ESP-IDF + install toolchain
#   make build        # compile
#   make flash        # flash the board (set PORT=... if not /dev/ttyACM0)
#   make monitor      # serial console (Ctrl+] to quit)
#   make run          # flash + monitor in one step
#   make help         # full target list
#
# All targets honour these variables:
#   PORT      serial device (default: /dev/ttyACM0)
#   BAUD      flash baud rate (default: 460800)
#   IDF_PATH  ESP-IDF checkout location (default: ~/esp/esp-idf)
#   IDF_VER   ESP-IDF tag to install (default: v5.3.1)

# ---------------------------------------------------------------- config
PORT     ?= /dev/ttyACM0
BAUD     ?= 460800
IDF_PATH ?= $(HOME)/esp/esp-idf
IDF_VER  ?= v5.3.1
TARGET   ?= esp32s3

# idf.py wants IDF_PATH exported and its tool venv on PATH.  Sourcing
# export.sh inside a recipe is cumbersome from /bin/sh because it uses
# bash-isms, so we invoke bash explicitly and re-export afterwards.
IDF_EXPORT = . $(IDF_PATH)/export.sh >/dev/null
IDF        = $(IDF_EXPORT) && idf.py -B build

.DEFAULT_GOAL := help

# ---------------------------------------------------------------- help
.PHONY: help
help:
	@echo "Cue — ESP32-S3 firmware build wrapper"
	@echo
	@echo "Targets:"
	@echo "  deps         Install host build deps (Fedora dnf)"
	@echo "  install      Clone ESP-IDF $(IDF_VER) into $(IDF_PATH) and run install.sh"
	@echo "  check        Verify ESP-IDF is reachable"
	@echo "  configure    Run idf.py set-target $(TARGET) (one-off)"
	@echo "  build        Compile firmware"
	@echo "  flash        Flash to PORT=$(PORT) at $(BAUD) baud"
	@echo "  monitor      Open serial monitor on $(PORT)"
	@echo "  run          flash + monitor"
	@echo "  erase        Full-chip erase"
	@echo "  menuconfig   Interactive sdkconfig editor"
	@echo "  size         Print binary size breakdown"
	@echo "  clean        Remove build/"
	@echo "  fullclean    Remove build/ AND sdkconfig"
	@echo
	@echo "Variables (override on the command line):"
	@echo "  PORT=$(PORT)  BAUD=$(BAUD)  IDF_PATH=$(IDF_PATH)  TARGET=$(TARGET)"

# ---------------------------------------------------------------- host deps
# Fedora package list per the ESP-IDF "Standard Setup of Toolchain for
# Linux" guide.  Safe to re-run; dnf will skip already-installed packages.
.PHONY: deps
deps:
	sudo dnf install -y git wget flex bison gperf python3 python3-pip \
	    cmake ninja-build ccache dfu-util libusbx

# ---------------------------------------------------------------- bootstrap
.PHONY: install
install:
	@if [ -d "$(IDF_PATH)" ]; then \
	  echo "ESP-IDF already at $(IDF_PATH) — skipping clone."; \
	else \
	  echo "Cloning ESP-IDF $(IDF_VER) → $(IDF_PATH)..."; \
	  mkdir -p $(dir $(IDF_PATH)); \
	  git clone --depth 1 --branch $(IDF_VER) --recursive \
	    https://github.com/espressif/esp-idf.git $(IDF_PATH); \
	fi
	@echo "Running ESP-IDF install.sh for $(TARGET)..."
	@bash -c '$(IDF_PATH)/install.sh $(TARGET)'
	@echo
	@echo "Done.  Now run:  make configure && make build"

.PHONY: check
check:
	@if [ ! -f "$(IDF_PATH)/export.sh" ]; then \
	  echo "ESP-IDF not found at $(IDF_PATH)."; \
	  echo "Run:  make install"; \
	  exit 1; \
	fi
	@bash -c '$(IDF_EXPORT) && idf.py --version'

# ---------------------------------------------------------------- build
.PHONY: configure
configure: check
	@bash -c '$(IDF) set-target $(TARGET)'

build/.target: | check
	@bash -c '$(IDF) set-target $(TARGET)'
	@mkdir -p build && touch build/.target

.PHONY: build
build: build/.target
	@bash -c '$(IDF) build'

.PHONY: menuconfig
menuconfig: build/.target
	@bash -c '$(IDF) menuconfig'

.PHONY: size
size: build/.target
	@bash -c '$(IDF) size'

# ---------------------------------------------------------------- flash / run
.PHONY: flash
flash: build
	@bash -c '$(IDF) -p $(PORT) -b $(BAUD) flash'

.PHONY: monitor
monitor: check
	@bash -c '$(IDF) -p $(PORT) monitor'

.PHONY: run
run: build
	@bash -c '$(IDF) -p $(PORT) -b $(BAUD) flash && $(IDF_EXPORT) && idf.py -B build -p $(PORT) monitor'

.PHONY: erase
erase: check
	@bash -c '$(IDF) -p $(PORT) erase-flash'

# ---------------------------------------------------------------- clean
.PHONY: clean
clean:
	rm -rf build

.PHONY: fullclean
fullclean:
	rm -rf build sdkconfig sdkconfig.old managed_components dependencies.lock
