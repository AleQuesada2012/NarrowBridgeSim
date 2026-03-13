# ==============================
# Platform detection
# ==============================
#
# Supported environments:
#   - Apple Silicon Mac  (Darwin + arm64)
#       XQuartz  → /opt/X11
#       Homebrew → /opt/homebrew
#   - x86-64 Ubuntu Linux
#       X11 headers/libs in standard system paths (no extra flags needed)
#
# Detection uses $(shell uname -s) for the OS and
# $(shell uname -m) for the architecture.
# ==============================

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# Flags added only to the GUI compile+link steps (not the simulation)
GUI_EXTRA_CFLAGS  =
GUI_EXTRA_LDFLAGS =

ifeq ($(UNAME_S),Darwin)
  ifeq ($(UNAME_M),arm64)
    # Apple Silicon: XQuartz under /opt/X11, Homebrew under /opt/homebrew
    GUI_EXTRA_CFLAGS  = -I/opt/X11/include -I/opt/homebrew/include
    GUI_EXTRA_LDFLAGS = -L/opt/X11/lib     -L/opt/homebrew/lib
  else
    # Intel Mac: XQuartz under /opt/X11, Homebrew under /usr/local
    GUI_EXTRA_CFLAGS  = -I/opt/X11/include -I/usr/local/include
    GUI_EXTRA_LDFLAGS = -L/opt/X11/lib     -L/usr/local/lib
  endif
  # Suppress XQuartz/OpenGL deprecation noise emitted by system headers
  GUI_EXTRA_CFLAGS += -DGL_SILENCE_DEPRECATION
endif
# Linux (Ubuntu): X11 is in standard system paths — no extra flags required.

# ==============================
# Compiler configuration
# ==============================

CC = gcc

CFLAGS      = -Wall -Wextra -Werror -pthread -Iinclude
LDFLAGS     = -lm -pthread
GUI_LDFLAGS = $(GUI_EXTRA_LDFLAGS) -lX11 -lm -pthread

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

# All .c files under src/ feed the simulation.
# gui.c lives at the project root and is compiled separately.
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
# (no -Werror: X11 headers produce
#  pedantic warnings outside our control)
# ==============================

$(BUILD_DIR)/gui.o: $(GUI_SRC) | $(BUILD_DIR)
	$(CC) -Wall -Wextra -pthread $(GUI_EXTRA_CFLAGS) -c $< -o $@

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

# Run simulation piped into the GUI.
#
# macOS / XQuartz notes:
#   XQuartz does not automatically export DISPLAY into the shell that
#   launches it, so the variable is often unset when running from a
#   terminal.  We handle this in two steps:
#     1. `open -a XQuartz` starts XQuartz if it is not already running
#        (it is a no-op if it is already up).
#     2. DISPLAY is defaulted to :0, which is the socket XQuartz always
#        listens on (/tmp/.X11-unix/X0).  If the shell already has
#        DISPLAY set (e.g. the user exported it manually) that value is
#        kept thanks to the ${DISPLAY:-:0} fallback syntax.
#   On Linux DISPLAY is set by the desktop session; no intervention needed.
run-gui: all
	@echo "=== Launching simulation + GUI ==="
	@echo "=== Press Q or Escape in the GUI window to quit ==="
ifeq ($(UNAME_S),Darwin)
	@open -a XQuartz 2>/dev/null || true
	@sleep 0.5
	DISPLAY=$${DISPLAY:-:0} ./$(SIM_TARGET) $(CONFIG) | DISPLAY=$${DISPLAY:-:0} ./$(GUI_TARGET)
else
	./$(SIM_TARGET) $(CONFIG) | ./$(GUI_TARGET)
endif

clean:
	rm -rf $(BUILD_DIR) $(SIM_TARGET) $(GUI_TARGET)

re: clean all

.PHONY: all clean run run-gui re