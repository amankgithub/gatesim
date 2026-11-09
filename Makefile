# =============================================================================
# Makefile - GateSim (C99 application)
#
# Targets: all  run  clean
# =============================================================================

# ---- Toolchain (override on the command line: make CC=clang) ----------------
CC         := gcc

# ---- Project layout ---------------------------------------------------------
SRC_DIR    := src
BUILD_DIR  := build
TARGET     := gatesim

APP_BIN    := $(BUILD_DIR)/$(TARGET)

# ---- Flags ------------------------------------------------------------------
# Warning set: -Wall -Wextra -Wpedantic is the course minimum; the rest catch
# MISRA-relevant problems (shadowing, implicit conversions, missing prototypes,
# switch without default, VLAs, casts dropping const ...). -Werror keeps the
# build warning-free.
WARNINGS   := -Wall -Wextra -Wpedantic -Werror \
              -Wshadow -Wconversion -Wsign-conversion -Wcast-qual \
              -Wstrict-prototypes -Wmissing-prototypes -Wredundant-decls \
              -Wswitch-default -Wvla -Wundef -Wfloat-equal -Wformat=2

CFLAGS     := -std=c99 $(WARNINGS) -O2

# ---- Sources -> objects -----------------------------------------------------
SRC        := $(wildcard $(SRC_DIR)/*.c)
OBJ        := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC))

.PHONY: all run clean

# ---- Main targets -----------------------------------------------------------
all: $(APP_BIN)

run: $(APP_BIN)
	./$(APP_BIN)

clean:
	rm -rf $(BUILD_DIR)

# ---- Link -------------------------------------------------------------------
$(APP_BIN): $(OBJ)
	$(CC) $^ -o $@

# ---- Compile (pattern rules; mkdir -p $(@D) creates the folder of the target) ---
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
