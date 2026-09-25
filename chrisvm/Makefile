CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -g -I.
BUILD := ../build/chrisvm

SRCS := \
	cpu/common/state.c \
	cpu/common/cpuid.c \
	cpu/common/exceptions.c \
	cpu/emulator/flags.c \
	cpu/emulator/decode.c \
	cpu/emulator/operands.c \
	cpu/emulator/execute.c \
	cpu/emulator/mmu.c \
	cpu/emulator/chriscpu.c \
	cpu/hv/chrishv.c \
	buses/io.c \
	buses/mmio.c \
	devices/serial/serial.c \
	machine/machine.c \
	machine/boot.c \
	machine/config.c \
	debug/trace.c

OBJS := $(addprefix $(BUILD)/, $(SRCS:.c=.o))

.PHONY: all test clean

all: $(BUILD)/chrisvm $(BUILD)/arith.elf

$(BUILD)/chrisvm: $(OBJS) frontend/main.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ frontend/main.c $(OBJS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/arith.elf: guests/arith.asm guests/link.ld
	@mkdir -p $(BUILD)
	nasm -f elf64 -o $(BUILD)/arith.o guests/arith.asm
	ld -nostdlib -T guests/link.ld -o $@ $(BUILD)/arith.o

$(BUILD)/test_chrisvm: $(OBJS) tests/test_chrisvm.c $(BUILD)/arith.elf
	$(CC) $(CFLAGS) -DCHRIS_GUEST=\"$(BUILD)/arith.elf\" -o $@ tests/test_chrisvm.c $(OBJS)

test: $(BUILD)/test_chrisvm $(BUILD)/chrisvm $(BUILD)/arith.elf
	$(BUILD)/test_chrisvm
	$(BUILD)/chrisvm --headless $(BUILD)/arith.elf > $(BUILD)/arith.out
	grep -q '^OK$$' $(BUILD)/arith.out

clean:
	rm -rf $(BUILD)
