# =============================================================================
# Makefile - GateSim (C99 application + GoogleTest unit tests)
#
# Targets: all  run  test  clean  check  crosscheck  release  debug  coverage  print-vars
# =============================================================================

# ---- Toolchain --------------------------------------------------------------
CC         := gcc
CXX        := g++

# ---- Project layout ---------------------------------------------------------
SRC_DIR    := src
INC_DIR    := include
TEST_DIR   := tests
BUILD_ROOT := build
OUTPUT_DIR := output
TARGET     := gatesim

# ---- Build mode: make debug, or make MODE=debug (default release) -----------
# release: -O2 lets the compiler optimise; -DNDEBUG means #define NDEBUG (switches off assert)
# debug  : -g adds source-line information for gdb; -O0 keeps the code exactly as written
MODE ?= release
ifeq ($(MODE),release)
    MODE_FLAGS := -O2 -DNDEBUG
else ifeq ($(MODE),debug)
    MODE_FLAGS := -g -O0 -DDEBUG
else
    $(error MODE must be release or debug, got '$(MODE)')
endif

# Each mode gets its own folder, so switching modes always rebuilds correctly
BUILD_DIR  := $(BUILD_ROOT)/$(MODE)
APP_BIN    := $(BUILD_DIR)/$(TARGET)

# ---- Test build: make coverage (sets COVERAGE=1) adds gcov counters ---------
COVERAGE ?= 0
ifeq ($(COVERAGE),1)
    COV_FLAGS  := --coverage
    TEST_BUILD := $(BUILD_ROOT)/coverage
else
    COV_FLAGS  :=
    TEST_BUILD := $(BUILD_ROOT)/test
endif
TEST_BIN   := $(TEST_BUILD)/run_tests

# ---- Flags ------------------------------------------------------------------
# Warning set: -Wall -Wextra -Wpedantic is the course minimum; the rest catch
# MISRA-relevant problems (shadowing, implicit conversions, missing prototypes,
# switch without default, VLAs, casts dropping const ...). -Werror keeps the
# build warning-free.
WARNINGS   := -Wall -Wextra -Wpedantic -Werror \
              -Wshadow -Wconversion -Wsign-conversion -Wcast-qual \
              -Wstrict-prototypes -Wmissing-prototypes -Wredundant-decls \
              -Wswitch-default -Wvla -Wundef -Wfloat-equal -Wformat=2

# -MMD -MP: the compiler also writes a .d file listing the headers each source uses
CPPFLAGS   := -I$(INC_DIR) -MMD -MP
CFLAGS     := -std=c99 $(WARNINGS) $(MODE_FLAGS)
TEST_CFLAGS   := -std=c99 $(WARNINGS) -g -O0 $(COV_FLAGS)
TEST_CXXFLAGS := -std=c++17 -Wall -Wextra -g -O0 $(COV_FLAGS)
# gtest_main needs symbols from gtest, so it is listed first (the linker reads left to right)
GTEST_LIBS := -lgtest_main -lgtest -pthread

# ---- Sources -> objects -----------------------------------------------------
SRC        := $(wildcard $(SRC_DIR)/*.c)
OBJ        := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC))

# Tests link every module except main.c (gtest_main supplies main())
LIB_SRC    := $(filter-out $(SRC_DIR)/main.c,$(SRC))
TEST_SRC   := $(wildcard $(TEST_DIR)/*.cpp)
TEST_OBJ   := $(patsubst $(SRC_DIR)/%.c,$(TEST_BUILD)/src/%.o,$(LIB_SRC)) \
              $(patsubst $(TEST_DIR)/%.cpp,$(TEST_BUILD)/tests/%.o,$(TEST_SRC))

DEPS       := $(OBJ:.o=.d) $(TEST_OBJ:.o=.d)

# Arguments of the program for make run (override: make run ARGS="...")
# demo: master-slave JK flip-flop from the library (race-around removed), waveform in output/waves.vcd
ARGS       ?= data/jk_ms.net data/jk_ms.stim --out $(OUTPUT_DIR)
# Options of the test program, e.g. make test GTEST_ARGS="--gtest_filter=<Suite>.*"
GTEST_ARGS ?=

.PHONY: all run test clean check crosscheck release debug coverage print-vars

# ---- Main targets -----------------------------------------------------------
all: $(APP_BIN)

run: $(APP_BIN)
	@mkdir -p $(OUTPUT_DIR)
	./$(APP_BIN) $(ARGS)

test: $(TEST_BIN)
	./$(TEST_BIN) $(GTEST_ARGS)

clean:
	rm -rf $(BUILD_ROOT) $(OUTPUT_DIR)

# Static analysis with the cppcheck MISRA C:2012 addon (needs cppcheck installed)
check:
	cppcheck --std=c99 --enable=warning,style,performance,portability \
	         --suppressions-list=tools/misra_suppressions.txt \
	         --addon=misra --inline-suppr --error-exitcode=1 \
	         --suppress=missingIncludeSystem -q -I$(INC_DIR) $(SRC_DIR)

# Every demo circuit in GateSim and in Icarus Verilog, waveforms compared (needs iverilog and python3)
crosscheck: $(APP_BIN)
	sh tools/crosscheck.sh $(APP_BIN)

# ---- Build modes and coverage -----------------------------------------------
release:
	$(MAKE) MODE=release all

debug:
	$(MAKE) MODE=debug all

# Line coverage per source file: tests rebuilt with --coverage, then gcov
coverage:
	$(MAKE) COVERAGE=1 test
	gcov -n -o $(BUILD_ROOT)/coverage/src $(LIB_SRC)

# ---- Link -------------------------------------------------------------------
$(APP_BIN): $(OBJ)
	$(CC) $^ -o $@

$(TEST_BIN): $(TEST_OBJ)
	$(CXX) $(COV_FLAGS) $^ $(GTEST_LIBS) -o $@

# ---- Compile (pattern rules; mkdir -p $(@D) creates the folder of the target) ---
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TEST_BUILD)/src/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -c $< -o $@

$(TEST_BUILD)/tests/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(TEST_CXXFLAGS) -c $< -o $@

# ---- Header dependencies: load the .d files (the '-' ignores missing ones) ---
-include $(DEPS)

# ---- Show the variables (make print-vars) -----------------------------------
print-vars:
	@echo "MODE       = $(MODE)"
	@echo "CFLAGS     = $(CFLAGS)"
	@echo "CPPFLAGS   = $(CPPFLAGS)"
	@echo "SRC        = $(SRC)"
	@echo "OBJ        = $(OBJ)"
	@echo "TEST_OBJ   = $(TEST_OBJ)"
	@echo "APP_BIN    = $(APP_BIN)"
	@echo "TEST_BIN   = $(TEST_BIN)"
