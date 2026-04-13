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
	@echo "     $(COLOR_CYAN)./$(TARGET) --mock mock.json target/your_library.so$(COLOR_RESET)"
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
	@rm -rf $(BUILD_DIR)/*.o $(BUILD_DIR)/jni_harness_*
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
	@echo "  make run SO=target/libnative.so"
	@echo "  make check-arch"
	@echo "  make cross-arm64"
	@echo ""
	@echo "$(COLOR_CYAN)Mock config:$(COLOR_RESET)"
	@echo "  $(COLOR_GREEN)./$(TARGET) --mock mock.json target/libnative.so$(COLOR_RESET)"
	@echo "  Use --mock/-m to inject return values for specific JNI method calls"
	@echo ""

# Dependency tracking
-include $(OBJECTS:.o=.d)

# Generate dependencies
$(BUILD_DIR)/%.d: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	@$(CC) $(CFLAGS) -MM -MT $(BUILD_DIR)/$*.o $< > $@

.PHONY: all directories show-info check-arch run test cross-arm64 cross-x86_64 clean clean-all install-deps help
