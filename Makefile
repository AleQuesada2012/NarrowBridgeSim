# ==============================
# Platform detection
# ==============================
#
# Supported environments:
#   - Apple Silicon Mac  (Darwin + arm64)  — SDL2 via Homebrew
#   - x86-64 Ubuntu Linux                 — SDL2 via apt
#
# sdl2-config is the canonical way to get SDL2 flags on both platforms.
# It lives in different places depending on how SDL2 was installed, so
# we locate it with $(shell which ...) and fall back to pkg-config, then
# to bare -lSDL2 if neither tool is available.
# ==============================

UNAME_S := $(shell uname -s)

SDL2_CONFIG := $(shell which sdl2-config 2>/dev/null)

ifdef SDL2_CONFIG
  SDL2_CFLAGS := $(shell $(SDL2_CONFIG) --cflags)
  SDL2_LIBS   := $(shell $(SDL2_CONFIG) --libs)
else
  # pkg-config fallback (common on Ubuntu when sdl2-config is not in PATH)
  SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
  SDL2_LIBS   := $(shell pkg-config --libs   sdl2 2>/dev/null)
endif

# Last-resort bare flags if neither tool found
ifeq ($(SDL2_LIBS),)
  SDL2_LIBS = -lSDL2
endif

# macOS: SDL2 from Homebrew may not be on the default search path.
# Add the Homebrew prefix so the linker can find libSDL2.
ifeq ($(UNAME_S),Darwin)
  HOMEBREW_PREFIX := $(shell brew --prefix sdl2 2>/dev/null)
  ifdef HOMEBREW_PREFIX
    SDL2_CFLAGS += -I$(HOMEBREW_PREFIX)/include
    SDL2_LIBS   += -L$(HOMEBREW_PREFIX)/lib
  endif
endif

# ==============================
# Compiler configuration
# ==============================

CC = gcc

CFLAGS   = -Wall -Wextra -Werror -pthread -Iinclude
LDFLAGS  = -lm -pthread

# GUI uses SDL2; no -Werror since SDL2 headers produce pedantic warnings
# on some platforms that are outside our control.
GUI_CFLAGS  = -Wall -Wextra -pthread $(SDL2_CFLAGS)
GUI_LDFLAGS = $(SDL2_LIBS) -lm -pthread

SIM_TARGET = bridge_sim
GUI_TARGET = bridge_gui

# ==============================
# Directories
# ==============================

SRC_DIR   = src
INC_DIR   = include
BUILD_DIR = build

# ==============================
# Source discovery
# ==============================

SIM_SRC = $(wildcard $(SRC_DIR)/*.c)
SIM_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SIM_SRC))

GUI_SRC = gui.c
GUI_OBJ = $(BUILD_DIR)/gui.o

CONFIG ?= bridge.config

# ==============================
# Default target
# ==============================

all: $(SIM_TARGET) $(GUI_TARGET)

# ==============================
# Link executables
# ==============================

$(SIM_TARGET): $(SIM_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

$(GUI_TARGET): $(GUI_OBJ)
	$(CC) $^ -o $@ $(GUI_LDFLAGS)

# ==============================
# Compile simulation objects
# ==============================

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ==============================
# Compile GUI object
# ==============================

$(BUILD_DIR)/gui.o: $(GUI_SRC) | $(BUILD_DIR)
	$(CC) $(GUI_CFLAGS) -c $< -o $@

# ==============================
# Ensure build directory exists
# ==============================

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ==============================
# Utility targets
# ==============================

# Run simulation only (terminal output)
run: $(SIM_TARGET)
	./$(SIM_TARGET) $(CONFIG)

# Run simulation piped into the GUI
run-gui: all
	@echo "=== Launching simulation + GUI ==="
	@echo "=== Press Q or Escape in the GUI window to quit ==="
	./$(SIM_TARGET) $(CONFIG) | ./$(GUI_TARGET)

clean:
	rm -rf $(BUILD_DIR) $(SIM_TARGET) $(GUI_TARGET)

re: clean all

.PHONY: all clean run run-gui re