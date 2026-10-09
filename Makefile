# Thin wrapper over CMake, so the common things are one word.
#
#   make          configure, build and run the game
#   make tests    build and run every unit test
#   make build    build the game without running it
#   make clean    delete the build directory
#
# CMake generates its own makefiles inside $(BUILD_DIR); this one only drives it.

BUILD_DIR ?= build
# Optimised, with debug info kept so a debugger still works. A Debug build of
# the game and of raylib runs the meshing and the draw loop several times
# slower; ask for one when stepping through code: make BUILD_TYPE=Debug
BUILD_TYPE ?= RelWithDebInfo

# One job per core, however the platform counts them
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# Extra flags for a single run, e.g.
#   make tests CTESTFLAGS="-R attach"
#   make tests CTESTFLAGS="--rerun-failed"
CTESTFLAGS ?=

.PHONY: all run build tests test configure reconfigure clean rebuild help

all: run

# --- Configuring ---

# A stamp per build type, so changing BUILD_TYPE (or the default above)
# configures again rather than quietly building with the cached one
CONFIGURED := $(BUILD_DIR)/.configured-$(BUILD_TYPE)

$(CONFIGURED):
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	@rm -f $(BUILD_DIR)/.configured-*
	@touch $@

configure: $(CONFIGURED)

# For when the cache itself is the problem
reconfigure:
	rm -rf $(BUILD_DIR)/CMakeCache.txt $(BUILD_DIR)/CMakeFiles $(BUILD_DIR)/.configured-*
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	@touch $(CONFIGURED)

# --- The game ---

build: configure
	cmake --build $(BUILD_DIR) --target business_game -j $(JOBS)

# Run from inside the build directory, not from bin: main.cpp loads its shaders
# from "../resources/...", which only resolves to the repository's resources
# folder when the working directory is one level below the repository root.
run: build
	cd $(BUILD_DIR) && ./bin/business_game

# --- The tests ---

# Only the test targets are built, so this does not wait for the game to link.
tests: configure
	cmake --build $(BUILD_DIR) --target business_game_tests business_game_sim_tests -j $(JOBS)
	ctest --test-dir $(BUILD_DIR) --output-on-failure $(CTESTFLAGS)

# Because both are worth typing
test: tests

# --- Housekeeping ---

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean all

help:
	@echo "make            configure, build and run the game"
	@echo "make tests      build and run every unit test"
	@echo "make build      build the game without running it"
	@echo "make reconfigure  re-run cmake, keeping the fetched dependencies"
	@echo "make clean      delete $(BUILD_DIR)"
	@echo ""
	@echo "Variables: BUILD_DIR=$(BUILD_DIR) BUILD_TYPE=$(BUILD_TYPE) JOBS=$(JOBS)"
	@echo "Filter tests:  make tests CTESTFLAGS=\"-R attach\""
