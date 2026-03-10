# ==============================
# Compiler configuration
# ==============================

CC = gcc

CFLAGS = -Wall -Wextra -Werror -pthread -Iinclude
LDFLAGS = -lm -pthread

TARGET = bridge_sim

# ==============================
# Directories
# ==============================

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# ==============================
# Source discovery
# ==============================

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC))

# ==============================
# Default target
# ==============================

all: $(TARGET)

# ==============================
# Link executable
# ==============================

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

# ==============================
# Compile objects
# ==============================

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ==============================
# Ensure build directory exists
# ==============================

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ==============================
# Utility targets
# ==============================

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

run: $(TARGET)
	./$(TARGET)

re: clean all

.PHONY: all clean run re