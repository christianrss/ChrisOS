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

C_OBJECTS := kernel/start.o

.PHONY: all iso run clean

all: iso

iso: $(ISO)

kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(C_OBJECTS) kernel/linker.ld
	mkdir -p $(ISO_ROOT)/boot
	$(LD) $(LDFLAGS) -o $@ $(C_OBJECTS)

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
