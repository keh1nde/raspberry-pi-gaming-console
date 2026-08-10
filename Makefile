# raspberry-pi-gaming-console Makefile
# Target: Raspberry Pi 5 (BCM2712, Cortex-A76), AArch64 bare-metal.

# --- Toolchain ---
CC       = aarch64-elf-gcc
CXX      = aarch64-elf-g++
AS       = aarch64-elf-gcc
OBJCOPY  = aarch64-elf-objcopy

# Project-local newlib install (see p-docs/newlib-port-roadmap.md) — built
# out-of-tree from source; not bundled with aarch64-elf-gcc itself. Must
# exist locally before building; not checked into git (see .gitignore).
NEWLIB_DIR = toolchain/newlib/aarch64-elf

# --- Flags ---
# -mcpu=cortex-a76: target the Pi 5's application cores
# -ffreestanding: no hosted-environment assumptions. Still correct with
#   newlib linked in — there's no real OS underneath (no argv, no
#   exit-to-shell) — it only means "don't assume the standard library is
#   present unless I ask for it."
# -fno-exceptions -fno-rtti: required for bare-metal C++ (no runtime support)
# -Wall: enable all warnings
COMMON_FLAGS = -mcpu=cortex-a76 -ffreestanding -Wall -g
ASFLAGS  = $(COMMON_FLAGS)
INCLUDES = -Iinclude -Ilib/include -I$(NEWLIB_DIR)/include
CXXFLAGS = $(COMMON_FLAGS) -fno-exceptions -fno-rtti $(INCLUDES)
# Plain C, for vendored third-party sources (FatFs) — -fno-exceptions/-fno-rtti
# are C++-only, so CFLAGS doesn't inherit them from CXXFLAGS.
CFLAGS   = $(COMMON_FLAGS) $(INCLUDES)
# -nostartfiles: keep boot.S's _start as the real entry point and skip
#   newlib's own crt0 — but unlike the old -nostdlib, this still lets
#   -lc/-lgcc resolve automatically via the compiler driver's default link
#   spec. -lm (math) is never linked automatically by any GCC config, hence
#   explicit here.
# -nostdlib++: g++ (unlike gcc) auto-links libstdc++ for any C++ link —
#   this toolchain was never built with one for this freestanding target,
#   so skip just that auto-link while still keeping -lc/-lgcc automatic.
LDFLAGS  = -nostartfiles -nostdlib++ -L$(NEWLIB_DIR)/lib -lc -lm

# --- Files ---
LINKER   = linker.ld
TARGET   = kernel8.img
ELF      = kernel8.elf

# Recursively discover every source under src/. Any new component directory
# (e.g. src/drivers/, src/userspace/) is picked up automatically — just drop
# the files in and rebuild.
ASM_SRC := $(shell find src -name '*.S')
CXX_SRC := $(shell find src -name '*.cpp')
# .c: plain C, for vendored third-party sources (FatFs) and their kernel-side
# glue (src/kernel/fatfs_retarget/diskio.c) — everything else in the tree is
# .cpp, so this is its own class of source rather than folded into CXX_SRC.
C_SRC   := $(shell find src -name '*.c')

# lib/ holds cross-cutting library code (not kernel-specific — usable by
# later userspace too), kept in its own include/src tree rather than nested
# under src/kernel. Discovered the same way as src/.
LIB_ASM_SRC := $(shell find lib/src -name '*.S')
LIB_CXX_SRC := $(shell find lib/src -name '*.cpp')
LIB_C_SRC   := $(shell find lib/src -name '*.c')

# FatFs's own sources (lib/src/fatfs/) use unqualified #include "ff.h",
# expecting headers alongside sources — upstream's own flat-directory
# convention, kept as-is rather than edited to match ours. Scoped to just
# that directory's objects so the rest of the tree doesn't gain an
# unqualified-include path.
build/lib/fatfs/%.o: CFLAGS += -Ilib/include/lib/fatfs

# Map source paths to object paths under build/ (mirrors the src/ and
# lib/src/ trees).
ASM_OBJ  = $(patsubst src/%.S, build/%.o, $(ASM_SRC)) \
           $(patsubst lib/src/%.S, build/lib/%.o, $(LIB_ASM_SRC))
CXX_OBJ  = $(patsubst src/%.cpp, build/%.o, $(CXX_SRC)) \
           $(patsubst lib/src/%.cpp, build/lib/%.o, $(LIB_CXX_SRC))
C_OBJ    = $(patsubst src/%.c, build/%.o, $(C_SRC)) \
           $(patsubst lib/src/%.c, build/lib/%.o, $(LIB_C_SRC))
OBJ      = $(ASM_OBJ) $(CXX_OBJ) $(C_OBJ)

# QEMU machine for `make run` / `make debug`.
QEMU_MACHINE = -M virt

# --- Targets ---
.PHONY: all run debug clean rebuild test test-debug

all: $(TARGET)

# Link all object files into an ELF, then strip to a raw binary image.
# The Pi's bootloader loads kernel8.img as a flat binary at 0x80000.
# Linked via the g++ driver (not raw ld) so -lc/-lm/-lgcc resolve without
# hand-tracking libgcc.a's path across multilib/CPU variants.
$(ELF): $(OBJ) $(LINKER)
	$(CXX) -Wl,-T,$(LINKER) -o $(ELF) $(OBJ) $(LDFLAGS)

$(TARGET): $(ELF)
	$(OBJCOPY) -O binary $(ELF) $(TARGET)

# Assemble .S files.
build/%.o: src/%.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# Compile .cpp files.
build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile .c files.
build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble/compile lib/ sources (see LIB_ASM_SRC/LIB_CXX_SRC/LIB_C_SRC above).
build/lib/%.o: lib/src/%.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

build/lib/%.o: lib/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/lib/%.o: lib/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

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
