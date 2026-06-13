# raspberry-pi-gaming-console Makefile
# Target: Raspberry Pi 5 (BCM2712, Cortex-A76), AArch64 bare-metal.

# --- Toolchain ---
CC       = aarch64-elf-gcc
CXX      = aarch64-elf-g++
AS       = aarch64-elf-gcc
LD       = aarch64-elf-ld
OBJCOPY  = aarch64-elf-objcopy

# --- Flags ---
# -mcpu=cortex-a76: target the Pi 5's application cores
# -ffreestanding -nostdlib: no standard library or startup files assumed
# -fno-exceptions -fno-rtti: required for bare-metal C++ (no runtime support)
# -Wall: enable all warnings
COMMON_FLAGS = -mcpu=cortex-a76 -ffreestanding -nostdlib -Wall
ASFLAGS  = $(COMMON_FLAGS)
CXXFLAGS = $(COMMON_FLAGS) -fno-exceptions -fno-rtti
LDFLAGS  = -nostdlib

# --- Files ---
LINKER   = linker.ld
TARGET   = kernel8.img
ELF      = kernel8.elf

# Recursively discover every source under src/. Any new component directory
# (e.g. src/drivers/, src/userspace/) is picked up automatically — just drop
# the files in and rebuild.
ASM_SRC := $(shell find src -name '*.S')
CXX_SRC := $(shell find src -name '*.cpp')

# Map source paths to object paths under build/ (mirrors the src/ tree).
ASM_OBJ  = $(patsubst src/%.S, build/%.o, $(ASM_SRC))
CXX_OBJ  = $(patsubst src/%.cpp, build/%.o, $(CXX_SRC))
OBJ      = $(ASM_OBJ) $(CXX_OBJ)

# QEMU machine for `make run` / `make debug`.
QEMU_MACHINE = -M virt

# --- Targets ---
.PHONY: all run debug clean rebuild test test-debug

all: $(TARGET)

# Link all object files into an ELF, then strip to a raw binary image.
# The Pi's bootloader loads kernel8.img as a flat binary at 0x80000.
$(ELF): $(OBJ) $(LINKER)
	$(LD) $(LDFLAGS) -T $(LINKER) -o $(ELF) $(OBJ)

$(TARGET): $(ELF)
	$(OBJCOPY) -O binary $(ELF) $(TARGET)

# Assemble .S files.
build/%.o: src/%.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# Compile .cpp files.
build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Iinclude -c $< -o $@

# Launch in QEMU — serial output goes to your terminal.
run: $(TARGET)
	qemu-system-aarch64 $(QEMU_MACHINE) -kernel $(TARGET) -serial stdio -display none

debug: $(TARGET)
	qemu-system-aarch64 $(QEMU_MACHINE) -kernel $(TARGET) -serial stdio -display none -d int 2>qemu_log.txt

clean:
	rm -rf build $(ELF) $(TARGET)

rebuild:
	$(MAKE) clean
	$(MAKE) all

test:
	$(MAKE) clean
	$(MAKE) all
	$(MAKE) run

test-debug:
	$(MAKE) clean
	$(MAKE) all
	$(MAKE) debug
