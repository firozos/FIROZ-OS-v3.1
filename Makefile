# ============================================================================
# FIROZ OS Payload - Makefile with Custom Linker Script
# PS5 Jailbreak Payload Compiler
# ============================================================================

# ============================================================================
# Configuration
# ============================================================================

# PS5 Payload SDK path (can be overridden via command line)
PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk

# Include the SDK's toolchain rules (compiler, flags, etc.)
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

# ============================================================================
# Build Configuration
# ============================================================================

# Output ELF filename
TARGET := FIROZ_OS.elf

# Source files
SRCS := main.c

# Compiler optimization and warning flags
CFLAGS := -O2 -Wall -Wextra -Wno-unused-parameter

# Linker flags
# -Wl,--no-dynamic-linker: Remove dynamic linker (not needed for PS5)
# -Wl,-T,linker.ld: Use custom linker script to separate segments
LDFLAGS := -Wl,--no-dynamic-linker -Wl,-T,linker.ld

# Libraries to link against
# -lkernel: Use standard kernel (NOT kernel_web)
# -lpthread: POSIX threads library
LDADD := -lkernel -lpthread

# ============================================================================
# Build Targets
# ============================================================================

# Default target
all: $(TARGET)

# Build the ELF executable
$(TARGET): $(SRCS) linker.ld
	@echo "[*] Compiling $(TARGET) with custom linker script..."
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDADD)
	@echo "[+] Build successful: $(TARGET)"
	@echo "[*] Verifying segment flags..."
	@readelf -l $(TARGET) | grep -A 1 "Type\|Flags" || true

# Clean build artifacts
clean:
	@echo "[*] Cleaning build artifacts..."
	rm -f $(TARGET)
	@echo "[+] Clean complete"

# Phony targets (not files)
.PHONY: all clean
