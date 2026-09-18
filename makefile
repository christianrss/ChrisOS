CC := gcc
LD := ld
QEMU := qemu-system-x86_64
XORRISO := xorriso
LIMINE_DIR := third_party/limine
ISO_ROOT := iso_root
KERNEL := $(ISO_ROOT)/boot/kernel.elf
ISO := os.iso

CFLAGS := -std=c11 -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-mno-red-zone -mcmodel=kernel -mno-mmx -mno-sse -mno-sse2 \
	-DLIMINE_API_REVISION=3 -I$(LIMINE_DIR) -Ikernel \
	-Wall -Wextra -Werror
LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
	-z noexecstack -T kernel/linker.ld

C_OBJECTS := kernel/start.o kernel/port.o kernel/serial.o kernel/panic.o \
	kernel/gdt.o kernel/idt.o kernel/irq.o kernel/pit.o kernel/ps2.o \
	kernel/bootinfo.o kernel/pmm.o kernel/mm.o kernel/heap.o \
	kernel/graphics.o kernel/font.o kernel/input.o kernel/task.o \
	kernel/ui.o kernel/desktop.o kernel/main.o \
	kernel/editor.o kernel/editor_window.o \
	kernel/ata_pio.o kernel/cfs.o kernel/cfs_fsck.o \
	kernel/storage.o kernel/fs.o
	
ASM_OBJECTS := kernel/idt_stubs.o
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)

HOST_CC := gcc
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -Ikernel

.PHONY: all iso run clean

all: iso

iso: $(ISO)

host-fsck-test: tools/test_cfs_fsck.c kernel/cfs.c kernel/cfs_fsck.c
	gcc -std=c11 -Wall -Wextra -Werror -Ikernel \
		-o tools/test_cfs_fsck tools/test_cfs_fsck.c \
		kernel/cfs.c kernel/cfs_fsck.c
	./tools/test_cfs_fsck

host-input-test: tools/test_input.c kernel/input.c kernel/input.h
	gcc -std=c11 -Wall -Wextra -Werror -Ikernel -o tools/test_input tools/test_input.c kernel/input.c
	./tools/test_input

host-editor-test: host/test_editor64.c kernel/editor.c kernel/editor.h
	$(HOST_CC) $(HOST_CFLAGS) -o host/test_editor64 host/test_editor64.c kernel/editor.c
	./host/test_editor64

kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/%.o: kernel/%.asm
	nasm -f elf64 $< -o $@

$(KERNEL): $(OBJECTS) kernel/linker.ld
	mkdir -p $(ISO_ROOT)/boot
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(ISO_ROOT)/boot/limine/limine-bios-cd.bin: $(LIMINE_DIR)/limine-bios-cd.bin
	mkdir -p $(ISO_ROOT)/boot/limine
	cp $< $@

$(ISO_ROOT)/boot/limine/limine-bios.sys: $(LIMINE_DIR)/limine-bios.sys
	mkdir -p $(ISO_ROOT)/boot/limine
	cp $< $@

$(ISO_ROOT)/boot/limine/limine-uefi-cd.bin: $(LIMINE_DIR)/limine-uefi-cd.bin
	mkdir -p $(ISO_ROOT)/boot/limine
	cp $< $@

$(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI: $(LIMINE_DIR)/BOOTX64.EFI
	mkdir -p $(ISO_ROOT)/EFI/BOOT
	cp $< $@

$(ISO): $(KERNEL) $(ISO_ROOT)/boot/limine/limine.conf \
	$(ISO_ROOT)/boot/limine/limine-bios-cd.bin \
	$(ISO_ROOT)/boot/limine/limine-bios.sys \
	$(ISO_ROOT)/boot/limine/limine-uefi-cd.bin \
	$(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	$(XORRISO) -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image \
		--protective-msdos-label $(ISO_ROOT) -o $@
	$(LIMINE_DIR)/limine bios-install $@

run: $(ISO)
	$(QEMU) -M q35 -m 256M -cdrom $(ISO) -serial stdio \
		-no-reboot -no-shutdown

clean:
	rm -f kernel/*.o $(KERNEL) $(ISO)
