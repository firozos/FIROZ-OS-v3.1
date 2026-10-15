PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

TARGET := FIROZ_OS.elf
SRCS   := main.c
CFLAGS := -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS := -Wl,-z,separate-code -Wl,--as-needed
LDADD  := -lkernel -lpthread

all: $(TARGET)

$(TARGET): $(SRCS)
	@echo "[*] Compiling $(TARGET)..."
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDADD)
	@echo "[+] Build successful!"

clean:
	@echo "[*] Cleaning..."
	rm -f $(TARGET)

.PHONY: all clean
