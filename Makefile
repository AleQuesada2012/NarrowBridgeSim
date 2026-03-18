# ==============================
# Compiler configuration
# ==============================

CC = gcc

CFLAGS   = -Wall -Wextra -Werror -pthread -Iinclude
LDFLAGS  = -lm -pthread

TARGET = bridge_sim

# ==============================
# Directories
# ==============================

SRC_DIR   = src
INC_DIR   = include
BUILD_DIR = build

# ==============================
# Source discovery
# ==============================

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC))

CONFIG ?= bridge.config

# ==============================
# Default target
# ==============================

all: $(TARGET)

# ==============================
# Link
# ==============================

$(TARGET): $(OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

# ==============================
# Compile
# ==============================

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ==============================
# Ensure build dir exists
# ==============================

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ==============================
# Utility
# ==============================

run: $(TARGET)
	./$(TARGET) $(CONFIG)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

re: clean all

.PHONY: all clean run re