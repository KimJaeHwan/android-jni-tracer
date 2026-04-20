# JNI Harness Makefile
# Supports Linux, macOS, WSL2, and cross-compilation

# Architecture detection
UNAME_M := $(shell uname -m)
UNAME_S := $(shell uname -s)

# Compiler settings
CC ?= gcc
CFLAGS = -Wall -Wextra -g -O2 -I./include -I./src
LDFLAGS = -ldl

# Architecture-specific flags
ifeq ($(UNAME_M),x86_64)
    ARCH_SUFFIX = x86_64
    CFLAGS += -m64
else ifeq ($(UNAME_M),aarch64)
    ARCH_SUFFIX = arm64
else ifeq ($(UNAME_M),arm64)
    ARCH_SUFFIX = arm64
else ifeq ($(UNAME_M),i686)
    ARCH_SUFFIX = x86
    CFLAGS += -m32
else ifeq ($(UNAME_M),i386)
    ARCH_SUFFIX = x86
    CFLAGS += -m32
else ifeq ($(UNAME_M),armv7l)
    ARCH_SUFFIX = armv7
else
    ARCH_SUFFIX = $(UNAME_M)
endif

# OS-specific settings
ifeq ($(UNAME_S),Darwin)
    # macOS
    OS_NAME = macOS
    # macOS uses different linker flags
    LDFLAGS = -ldl
else ifeq ($(UNAME_S),Linux)
    # Linux
    OS_NAME = Linux
    LDFLAGS = -ldl -lpthread
else
    OS_NAME = $(UNAME_S)
endif

# Directories
SRC_DIR = src
BUILD_DIR = build
TARGET_DIR = target
LOG_DIR = logs
INCLUDE_DIR = include

# Android NDK build settings. Override these when needed:
#   make android-arm64 ANDROID_NDK=/path/to/ndk ANDROID_API=23
ANDROID_API ?= 21
ANDROID_ARCH ?= arm64
ANDROID_NDK ?= $(shell \
	if [ -n "$$ANDROID_NDK_HOME" ] && [ -d "$$ANDROID_NDK_HOME" ]; then \
		echo "$$ANDROID_NDK_HOME"; \
	elif [ -n "$$ANDROID_NDK_ROOT" ] && [ -d "$$ANDROID_NDK_ROOT" ]; then \
		echo "$$ANDROID_NDK_ROOT"; \
	elif [ -n "$$ANDROID_HOME" ] && [ -d "$$ANDROID_HOME/ndk" ]; then \
		ls -d "$$ANDROID_HOME"/ndk/* 2>/dev/null | sort | tail -n 1; \
	elif [ -n "$$ANDROID_SDK_ROOT" ] && [ -d "$$ANDROID_SDK_ROOT/ndk" ]; then \
		ls -d "$$ANDROID_SDK_ROOT"/ndk/* 2>/dev/null | sort | tail -n 1; \
	else \
		ls -d /opt/homebrew/Caskroom/android-ndk/*/*.app/Contents/NDK \
		      /usr/local/Caskroom/android-ndk/*/*.app/Contents/NDK \
		      /opt/android-sdk/ndk/* \
		      /usr/local/share/android-sdk/ndk/* 2>/dev/null | sort | tail -n 1; \
	fi)

ifeq ($(UNAME_S),Darwin)
    ANDROID_HOST_TAG = darwin-x86_64
else ifeq ($(UNAME_S),Linux)
    ANDROID_HOST_TAG = linux-x86_64
else
    ANDROID_HOST_TAG = unsupported
endif

ifeq ($(ANDROID_ARCH),arm64)
    ANDROID_TRIPLE = aarch64-linux-android
    ANDROID_OUTPUT_SUFFIX = arm64_android
else ifeq ($(ANDROID_ARCH),x86_64)
    ANDROID_TRIPLE = x86_64-linux-android
    ANDROID_OUTPUT_SUFFIX = x86_64_android
else
    $(error Unsupported ANDROID_ARCH=$(ANDROID_ARCH). Use arm64 or x86_64)
endif

ANDROID_TOOLCHAIN = $(ANDROID_NDK)/toolchains/llvm/prebuilt/$(ANDROID_HOST_TAG)
ANDROID_CC = $(ANDROID_TOOLCHAIN)/bin/$(ANDROID_TRIPLE)$(ANDROID_API)-clang
ANDROID_SYSROOT = $(ANDROID_TOOLCHAIN)/sysroot
ANDROID_BUILD_DIR = $(BUILD_DIR)/android-$(ANDROID_ARCH)
ANDROID_TARGET = $(BUILD_DIR)/jni_harness_$(ANDROID_OUTPUT_SUFFIX)
ANDROID_CFLAGS = -Wall -Wextra -g -O2 -I./include -I./src --sysroot=$(ANDROID_SYSROOT) -fPIC -DANDROID
ANDROID_LDFLAGS = -ldl -llog

# Source files
SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/fake_jni.c \
          $(SRC_DIR)/jni_logger.c \
          $(SRC_DIR)/json_logger.c \
          $(SRC_DIR)/mock_config.c

OBJECTS = $(BUILD_DIR)/main.o \
          $(BUILD_DIR)/fake_jni.o \
          $(BUILD_DIR)/jni_logger.o \
          $(BUILD_DIR)/json_logger.o \
          $(BUILD_DIR)/mock_config.o

ANDROID_OBJECTS = $(ANDROID_BUILD_DIR)/main.o \
                  $(ANDROID_BUILD_DIR)/fake_jni.o \
                  $(ANDROID_BUILD_DIR)/jni_logger.o \
                  $(ANDROID_BUILD_DIR)/json_logger.o \
                  $(ANDROID_BUILD_DIR)/mock_config.o

# Executable
TARGET = $(BUILD_DIR)/jni_harness_$(ARCH_SUFFIX)

# Colors for output
COLOR_RESET = \033[0m
COLOR_BOLD = \033[1m
COLOR_GREEN = \033[32m
COLOR_YELLOW = \033[33m
COLOR_BLUE = \033[34m
COLOR_CYAN = \033[36m

# Default target
.PHONY: all
all: directories $(TARGET) show-info

# Show build information
.PHONY: show-info
show-info:
	@echo ""
	@echo "$(COLOR_GREEN)$(COLOR_BOLD)===============================================$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)$(COLOR_BOLD)  Build Complete!$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)$(COLOR_BOLD)===============================================$(COLOR_RESET)"
	@echo "$(COLOR_CYAN)OS:           $(COLOR_RESET)$(OS_NAME)"
	@echo "$(COLOR_CYAN)Architecture: $(COLOR_RESET)$(UNAME_M) -> $(ARCH_SUFFIX)"
	@echo "$(COLOR_CYAN)Compiler:     $(COLOR_RESET)$(CC)"
	@echo "$(COLOR_CYAN)Executable:   $(COLOR_RESET)$(TARGET)"
	@echo "$(COLOR_GREEN)$(COLOR_BOLD)===============================================$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_YELLOW)Next steps:$(COLOR_RESET)"
	@echo "  1. Check target SO architecture:"
	@echo "     $(COLOR_CYAN)file target/your_library.so$(COLOR_RESET)"
	@echo ""
	@echo "  2. Verify architecture match:"
	@echo "     $(COLOR_CYAN)make check-arch$(COLOR_RESET)"
	@echo ""
	@echo "  3. Run the harness:"
	@echo "     $(COLOR_CYAN)./$(TARGET) target/your_library.so$(COLOR_RESET)"
	@echo "     $(COLOR_CYAN)./$(TARGET) target/your_library.so --mock mock.json$(COLOR_RESET)"
	@echo ""
	@echo "  4. View logs:"
	@echo "     $(COLOR_CYAN)cat logs/jni_hook.log$(COLOR_RESET)"
	@echo "     $(COLOR_CYAN)cat logs/jni_hook.json | head -50$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)$(COLOR_BOLD)===============================================$(COLOR_RESET)"
	@echo ""

# Create necessary directories
.PHONY: directories
directories:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(TARGET_DIR)
	@mkdir -p $(LOG_DIR)

# Link executable
$(TARGET): $(OBJECTS)
	@echo "$(COLOR_BLUE)[LINK]$(COLOR_RESET) $@"
	@$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

# Compile source files
$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c $(SRC_DIR)/fake_jni.h $(SRC_DIR)/jni_logger.h $(SRC_DIR)/json_logger.h $(INCLUDE_DIR)/jni.h
	@echo "$(COLOR_BLUE)[CC]$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fake_jni.o: $(SRC_DIR)/fake_jni.c $(SRC_DIR)/fake_jni.h $(SRC_DIR)/jni_logger.h $(SRC_DIR)/json_logger.h $(INCLUDE_DIR)/jni.h
	@echo "$(COLOR_BLUE)[CC]$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/jni_logger.o: $(SRC_DIR)/jni_logger.c $(SRC_DIR)/jni_logger.h
	@echo "$(COLOR_BLUE)[CC]$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/json_logger.o: $(SRC_DIR)/json_logger.c $(SRC_DIR)/json_logger.h
	@echo "$(COLOR_BLUE)[CC]$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_config.o: $(SRC_DIR)/mock_config.c $(SRC_DIR)/mock_config.h $(INCLUDE_DIR)/jni.h
	@echo "$(COLOR_BLUE)[CC]$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Build Android device binaries with Android NDK.
.PHONY: android android-arm64 android-x86_64 android-directories check-android-ndk show-android-info
android-arm64:
	@$(MAKE) android ANDROID_ARCH=arm64

android-x86_64:
	@$(MAKE) android ANDROID_ARCH=x86_64

android: check-android-ndk android-directories $(ANDROID_TARGET) show-android-info

android-directories:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(ANDROID_BUILD_DIR)
	@mkdir -p $(LOG_DIR)

check-android-ndk:
	@if [ "$(ANDROID_HOST_TAG)" = "unsupported" ]; then \
		echo "$(COLOR_YELLOW)Unsupported host OS for Android NDK build: $(UNAME_S)$(COLOR_RESET)"; \
		exit 1; \
	fi
	@if [ -z "$(ANDROID_NDK)" ] || [ ! -d "$(ANDROID_NDK)" ]; then \
		echo "$(COLOR_YELLOW)Android NDK not found.$(COLOR_RESET)"; \
		echo "Install Android NDK, then run one of:"; \
		echo "  make android-arm64 ANDROID_NDK=/absolute/path/to/ndk"; \
		echo "  ANDROID_NDK_HOME=/absolute/path/to/ndk make android-arm64"; \
		echo ""; \
		echo "Auto-detected candidates:"; \
		echo "  ANDROID_HOME=$$ANDROID_HOME"; \
		echo "  ANDROID_SDK_ROOT=$$ANDROID_SDK_ROOT"; \
		echo "  Homebrew Cask: /opt/homebrew/Caskroom/android-ndk"; \
		exit 1; \
	fi
	@if [ ! -x "$(ANDROID_CC)" ]; then \
		echo "$(COLOR_YELLOW)Android compiler not found: $(ANDROID_CC)$(COLOR_RESET)"; \
		echo "Try a different ANDROID_API, for example:"; \
		echo "  make android-arm64 ANDROID_API=23"; \
		exit 1; \
	fi

$(ANDROID_TARGET): $(ANDROID_OBJECTS)
	@echo "$(COLOR_BLUE)[ANDROID LINK]$(COLOR_RESET) $@"
	@$(ANDROID_CC) $(ANDROID_OBJECTS) -o $@ $(ANDROID_LDFLAGS)

$(ANDROID_BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "$(COLOR_BLUE)[ANDROID CC]$(COLOR_RESET) $<"
	@$(ANDROID_CC) $(ANDROID_CFLAGS) -c $< -o $@

show-android-info:
	@echo ""
	@echo "$(COLOR_GREEN)$(COLOR_BOLD)===============================================$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)$(COLOR_BOLD)  Android Build Complete!$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)$(COLOR_BOLD)===============================================$(COLOR_RESET)"
	@echo "$(COLOR_CYAN)Host OS:       $(COLOR_RESET)$(OS_NAME)"
	@echo "$(COLOR_CYAN)Android ABI:   $(COLOR_RESET)$(ANDROID_ARCH)"
	@echo "$(COLOR_CYAN)API Level:     $(COLOR_RESET)$(ANDROID_API)"
	@echo "$(COLOR_CYAN)NDK:           $(COLOR_RESET)$(ANDROID_NDK)"
	@echo "$(COLOR_CYAN)Compiler:      $(COLOR_RESET)$(ANDROID_CC)"
	@echo "$(COLOR_CYAN)Executable:    $(COLOR_RESET)$(ANDROID_TARGET)"
	@echo "$(COLOR_GREEN)$(COLOR_BOLD)===============================================$(COLOR_RESET)"
	@echo ""

# Check architecture compatibility
.PHONY: check-arch
check-arch:
	@echo "$(COLOR_YELLOW)=== Architecture Check ===$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_CYAN)System Architecture:$(COLOR_RESET)"
	@uname -m
	@echo ""
	@echo "$(COLOR_CYAN)Harness Executable:$(COLOR_RESET)"
	@if [ -f $(TARGET) ]; then \
		file $(TARGET); \
	else \
		echo "$(COLOR_YELLOW)Not built yet. Run 'make' first.$(COLOR_RESET)"; \
	fi
	@echo ""
	@echo "$(COLOR_CYAN)Target SO files in $(TARGET_DIR)/:$(COLOR_RESET)"
	@if [ -d $(TARGET_DIR) ]; then \
		find $(TARGET_DIR) -name "*.so" -exec echo "  {}" \; -exec file {} \; 2>/dev/null || echo "$(COLOR_YELLOW)No .so files found$(COLOR_RESET)"; \
	else \
		echo "$(COLOR_YELLOW)Target directory not found$(COLOR_RESET)"; \
	fi
	@echo ""
	@echo "$(COLOR_GREEN)Architecture must match between harness and SO!$(COLOR_RESET)"
	@echo ""

# Run with a test SO
.PHONY: run
run: $(TARGET)
	@if [ -z "$(SO)" ]; then \
		echo "$(COLOR_YELLOW)Usage: make run SO=path/to/library.so$(COLOR_RESET)"; \
		exit 1; \
	fi
	@echo "$(COLOR_GREEN)Running harness with: $(SO)$(COLOR_RESET)"
	@./$(TARGET) $(SO)

# Test with sample (if exists)
.PHONY: test
test: $(TARGET)
	@if [ -f $(TARGET_DIR)/test_sample.so ]; then \
		echo "$(COLOR_GREEN)Running test...$(COLOR_RESET)"; \
		./$(TARGET) $(TARGET_DIR)/test_sample.so; \
	else \
		echo "$(COLOR_YELLOW)No test_sample.so found in $(TARGET_DIR)/$(COLOR_RESET)"; \
		echo "Create a test SO or use: make run SO=path/to/your.so"; \
	fi

# Cross-compile for ARM64 (from x86_64)
.PHONY: cross-arm64
cross-arm64:
	@echo "$(COLOR_YELLOW)Cross-compiling for ARM64...$(COLOR_RESET)"
	@if ! command -v aarch64-linux-gnu-gcc > /dev/null 2>&1; then \
		echo "$(COLOR_YELLOW)aarch64-linux-gnu-gcc not found!$(COLOR_RESET)"; \
		echo "Install with: sudo apt-get install gcc-aarch64-linux-gnu"; \
		exit 1; \
	fi
	@$(MAKE) CC=aarch64-linux-gnu-gcc ARCH_SUFFIX=arm64 UNAME_M=aarch64

# Cross-compile for x86_64 (from ARM64)
.PHONY: cross-x86_64
cross-x86_64:
	@echo "$(COLOR_YELLOW)Cross-compiling for x86_64...$(COLOR_RESET)"
	@if ! command -v x86_64-linux-gnu-gcc > /dev/null 2>&1; then \
		echo "$(COLOR_YELLOW)x86_64-linux-gnu-gcc not found!$(COLOR_RESET)"; \
		echo "Install with: sudo apt-get install gcc-x86-64-linux-gnu"; \
		exit 1; \
	fi
	@$(MAKE) CC=x86_64-linux-gnu-gcc ARCH_SUFFIX=x86_64 UNAME_M=x86_64

# Clean build artifacts
.PHONY: clean
clean:
	@echo "$(COLOR_YELLOW)Cleaning build artifacts...$(COLOR_RESET)"
	@rm -rf $(BUILD_DIR)/*.o $(BUILD_DIR)/android-* $(BUILD_DIR)/jni_harness_*
	@echo "$(COLOR_GREEN)Clean complete$(COLOR_RESET)"

# Clean everything including logs
.PHONY: clean-all
clean-all: clean
	@echo "$(COLOR_YELLOW)Cleaning logs...$(COLOR_RESET)"
	@rm -rf $(LOG_DIR)/*.log $(LOG_DIR)/*.json
	@echo "$(COLOR_GREEN)Clean all complete$(COLOR_RESET)"

# Install dependencies (Ubuntu/Debian)
.PHONY: install-deps
install-deps:
	@echo "$(COLOR_YELLOW)Installing dependencies...$(COLOR_RESET)"
	@if command -v apt-get > /dev/null 2>&1; then \
		sudo apt-get update; \
		sudo apt-get install -y build-essential file; \
		echo "$(COLOR_GREEN)Dependencies installed$(COLOR_RESET)"; \
	elif command -v brew > /dev/null 2>&1; then \
		brew install gcc; \
		echo "$(COLOR_GREEN)Dependencies installed$(COLOR_RESET)"; \
	else \
		echo "$(COLOR_YELLOW)Package manager not found. Please install build-essential manually.$(COLOR_RESET)"; \
	fi

# Help
.PHONY: help
help:
	@echo "$(COLOR_BOLD)JNI Harness Makefile$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_CYAN)Common targets:$(COLOR_RESET)"
	@echo "  $(COLOR_GREEN)make$(COLOR_RESET)              - Build the harness for current architecture"
	@echo "  $(COLOR_GREEN)make android-arm64$(COLOR_RESET) - Build Android ARM64 device binary"
	@echo "  $(COLOR_GREEN)make android-x86_64$(COLOR_RESET) - Build Android x86_64 emulator binary"
	@echo "  $(COLOR_GREEN)make clean$(COLOR_RESET)        - Remove build artifacts"
	@echo "  $(COLOR_GREEN)make clean-all$(COLOR_RESET)    - Remove build artifacts and logs"
	@echo "  $(COLOR_GREEN)make check-arch$(COLOR_RESET)   - Check architecture compatibility"
	@echo "  $(COLOR_GREEN)make run SO=<path>$(COLOR_RESET) - Run harness with specified SO"
	@echo "  $(COLOR_GREEN)make test$(COLOR_RESET)         - Run with test_sample.so (if exists)"
	@echo ""
	@echo "$(COLOR_CYAN)Cross-compilation:$(COLOR_RESET)"
	@echo "  $(COLOR_GREEN)make cross-arm64$(COLOR_RESET)   - Cross-compile for ARM64 (aarch64)"
	@echo "  $(COLOR_GREEN)make cross-x86_64$(COLOR_RESET)  - Cross-compile for x86_64"
	@echo ""
	@echo "$(COLOR_CYAN)Other targets:$(COLOR_RESET)"
	@echo "  $(COLOR_GREEN)make install-deps$(COLOR_RESET) - Install build dependencies"
	@echo "  $(COLOR_GREEN)make help$(COLOR_RESET)         - Show this help message"
	@echo ""
	@echo "$(COLOR_CYAN)Examples:$(COLOR_RESET)"
	@echo "  make"
	@echo "  make android-arm64"
	@echo "  make android-arm64 ANDROID_NDK=/absolute/path/to/ndk"
	@echo "  make run SO=target/libnative.so"
	@echo "  make check-arch"
	@echo "  make cross-arm64"
	@echo ""
	@echo "$(COLOR_CYAN)Mock config:$(COLOR_RESET)"
	@echo "  $(COLOR_GREEN)./$(TARGET) target/libnative.so --mock mock.json$(COLOR_RESET)"
	@echo "  Use --mock/-m to inject return values for specific JNI method calls"
	@echo ""

# Dependency tracking
-include $(OBJECTS:.o=.d)

# Generate dependencies
$(BUILD_DIR)/%.d: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	@$(CC) $(CFLAGS) -MM -MT $(BUILD_DIR)/$*.o $< > $@

.PHONY: all directories show-info check-arch run test cross-arm64 cross-x86_64 clean clean-all install-deps help android android-arm64 android-x86_64
