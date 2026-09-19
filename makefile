CC := gcc
LD := ld
QEMU := qemu-system-x86_64
XORRISO := xorriso
LIMINE_DIR := third_party/limine
ISO_ROOT := iso_root
KERNEL := $(ISO_ROOT)/boot/kernel.elf
ISO := os.iso

KINC := -Ikernel/metal -Ikernel/gfx -Ikernel/wm -Ikernel/tools \
	-Ikernel/fs -Ikernel/lang -Ikernel/net -Icompiler -Icompiler/clvm \
	-Icompiler/jit

CFLAGS := -std=c11 -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-mno-red-zone -mcmodel=kernel -mno-mmx -mno-sse -mno-sse2 \
	-DLIMINE_API_REVISION=3 -I$(LIMINE_DIR) $(KINC) \
	-Wall -Wextra -Werror
LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
	-z noexecstack -T kernel/metal/linker.ld

C_OBJECTS := kernel/metal/start.o kernel/metal/port.o kernel/metal/serial.o \
	kernel/metal/panic.o kernel/metal/gdt.o kernel/metal/idt.o \
	kernel/metal/irq.o kernel/metal/syscall.o kernel/metal/user_enter.o \
	kernel/metal/elf.o kernel/metal/pit.o kernel/metal/ps2.o \
	kernel/metal/bootinfo.o kernel/metal/pmm.o kernel/metal/mm.o \
	kernel/metal/heap.o kernel/metal/pci.o \
	kernel/net/virtio_net.o kernel/net/net.o \
	kernel/gfx/graphics.o kernel/gfx/font.o kernel/gfx/input.o \
	kernel/gfx/speaker.o kernel/gfx/gfx2d.o \
	kernel/wm/task.o kernel/wm/ui.o kernel/wm/desktop.o kernel/wm/main.o \
	kernel/tools/editor.o kernel/tools/editor_window.o kernel/tools/explorer.o \
	kernel/tools/shell.o \
	kernel/fs/ata_pio.o kernel/fs/cfs.o kernel/fs/cfs_fsck.o \
	kernel/fs/storage.o kernel/fs/fs.o \
	kernel/lang/lang_sys.o kernel/lang/clvm_sys.o \
	compiler/lang_pipeline.o compiler/chrisc/chrisc.o \
	compiler/clvm/clasm.o compiler/clvm/clvm_format.o \
	compiler/clvm/clvm_vm.o kernel/tools/app_window.o \
	compiler/jit/jit.o compiler/jit/jit_emit.o compiler/jit/jit_compile.o \
	kernel/tools/taskmgr.o

ASM_OBJECTS := kernel/metal/idt_stubs.o
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)

HOST_CC := gcc
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -Ikernel/tools -Ikernel/fs

HOST_NET_PORT ?= 7007

.PHONY: all iso run run-stop clean disk disk.img host-gates

run-stop:
	-killall qemu-system-x86_64 2>/dev/null || pkill -x qemu-system-x86_64 2>/dev/null || true

all: iso

iso: $(ISO)

host-cfs-test: tools/test_cfs_host.c kernel/fs/cfs.c kernel/fs/cfs.h
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o tools/test_cfs_host tools/test_cfs_host.c kernel/fs/cfs.c
	./tools/test_cfs_host

host-fsck-test: tools/test_cfs_fsck.c kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o tools/test_cfs_fsck tools/test_cfs_fsck.c \
		kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	./tools/test_cfs_fsck

host-input-test: tools/test_input.c kernel/gfx/input.c kernel/gfx/input.h
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx \
		-o tools/test_input tools/test_input.c kernel/gfx/input.c
	./tools/test_input

host-editor-test: host/test_editor64.c kernel/tools/editor.c kernel/tools/editor.h
	$(HOST_CC) $(HOST_CFLAGS) -o host/test_editor64 \
		host/test_editor64.c kernel/tools/editor.c
	./host/test_editor64

test_gfx2d: kernel/gfx/gfx2d.c tools/test_gfx2d.c kernel/gfx/gfx2d.h
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx \
		kernel/gfx/gfx2d.c tools/test_gfx2d.c -o test_gfx2d

test_keystate: tools/test_keystate.c
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror tools/test_keystate.c -o test_keystate

host-cfs-paths-test: tools/test_cfs_paths.c kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o tools/test_cfs_paths tools/test_cfs_paths.c \
		kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	./tools/test_cfs_paths

host-cfs-indirect-test: tools/test_cfs_indirect.c kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o tools/test_cfs_indirect tools/test_cfs_indirect.c \
		kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	./tools/test_cfs_indirect

host-cfs-journal-test: tools/test_cfs_journal.c kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o tools/test_cfs_journal tools/test_cfs_journal.c \
		kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	./tools/test_cfs_journal

host-cfs-chmod: tools/cfs_chmod.c kernel/fs/cfs.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o tools/cfs_chmod tools/cfs_chmod.c kernel/fs/cfs.c

host-cfs-chmod-test: tools/test_cfs_chmod.c kernel/fs/cfs.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o tools/test_cfs_chmod tools/test_cfs_chmod.c kernel/fs/cfs.c
	./tools/test_cfs_chmod

host-cfs-put: tools/cfs_put.c kernel/fs/cfs.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o tools/cfs_put tools/cfs_put.c kernel/fs/cfs.c

host-cfs-mkdisk: tools/cfs_mkdisk.c kernel/fs/cfs.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o tools/cfs_mkdisk tools/cfs_mkdisk.c kernel/fs/cfs.c

disk.img: host-cfs-mkdisk
	./tools/cfs_mkdisk disk.img

user/hello.elf: user/hello.asm
	nasm -f bin user/hello.asm -o user/hello.elf

user/fault.elf: user/fault.asm
	nasm -f bin user/fault.asm -o user/fault.elf

host-cfs-put-file: tools/cfs_put_file.c kernel/fs/cfs.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o tools/cfs_put_file tools/cfs_put_file.c kernel/fs/cfs.c

disk-hello: disk.img user/hello.elf host-cfs-put-file
	./tools/cfs_put_file disk.img BIN/HELLO.ELF user/hello.elf

disk-fault: disk.img user/fault.elf host-cfs-put-file
	./tools/cfs_put_file disk.img BIN/FAULT.ELF user/fault.elf

disk: disk-hello disk-fault host-cfs-put
	./tools/cfs_put disk.img

host-gates: host-cfs-test host-fsck-test host-cfs-paths-test \
	host-cfs-indirect-test host-cfs-journal-test host-cfs-chmod-test \
	host-jit-test

host-cfs-migrate: tools/cfs_migrate_v1v2.c kernel/fs/cfs.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o tools/cfs_migrate_v1v2 tools/cfs_migrate_v1v2.c kernel/fs/cfs.c

host-jit-test: tools/test_jit_enc.c tools/jit_host_stub.c compiler/jit/jit_emit.c
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/jit \
		tools/jit_host_stub.c compiler/jit/jit_emit.c tools/test_jit_enc.c \
		-o tools/test_jit_enc
	./tools/test_jit_enc

kernel/metal/%.o: kernel/metal/%.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/gfx/%.o: kernel/gfx/%.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/wm/%.o: kernel/wm/%.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/tools/%.o: kernel/tools/%.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/fs/%.o: kernel/fs/%.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/lang/%.o: kernel/lang/%.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/net/%.o: kernel/net/%.c
	$(CC) $(CFLAGS) -c $< -o $@

compiler/%.o: compiler/%.c
	$(CC) $(CFLAGS) -c $< -o $@

compiler/chrisc/%.o: compiler/chrisc/%.c
	$(CC) $(CFLAGS) -c $< -o $@

compiler/clvm/%.o: compiler/clvm/%.c
	$(CC) $(CFLAGS) -c $< -o $@

compiler/jit/%.o: compiler/jit/%.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/metal/%.o: kernel/metal/%.asm
	nasm -f elf64 $< -o $@

$(KERNEL): $(OBJECTS) kernel/metal/linker.ld
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

run: $(ISO) run-stop
	@sleep 2
	$(QEMU) -M q35 -m 256M -boot d -cdrom $(ISO) \
		-drive file=disk.img,format=raw,if=ide,index=0,media=disk \
		-device virtio-net-pci,netdev=n0 \
		-netdev user,id=n0,hostfwd=udp:127.0.0.1:$(HOST_NET_PORT)-:7,hostfwd=tcp:127.0.0.1:$(HOST_NET_PORT)-:7 \
		-serial stdio -no-reboot -no-shutdown

clean:
	rm -f kernel/metal/*.o kernel/gfx/*.o kernel/wm/*.o kernel/tools/*.o \
		kernel/fs/*.o kernel/lang/*.o kernel/net/*.o compiler/*.o \
		compiler/chrisc/*.o compiler/clvm/*.o compiler/jit/*.o $(KERNEL) $(ISO)
