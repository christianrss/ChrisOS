CC := gcc
LD := ld
QEMU := qemu-system-x86_64
XORRISO := xorriso
LIMINE_DIR := third_party/limine

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
HOST_BIN := $(BUILD_DIR)/host
USER_BIN := $(BUILD_DIR)/user
ISO_ROOT := $(BUILD_DIR)/iso
KERNEL := $(ISO_ROOT)/boot/kernel.elf
ISO := $(BUILD_DIR)/os.iso
DISK_IMG := $(BUILD_DIR)/disk.img
LIMINE_CONF_SRC := iso_root/boot/limine/limine.conf

KINC := -Ikernel/metal -Ikernel/gfx -Ikernel/wm -Ikernel/tools \
	-Ikernel/fs -Ikernel/lang -Ikernel/net -Icompiler -Icompiler/clvm \
	-Icompiler/jit -Icompiler/chrisld -Icompiler/chrisasm -Icompiler/kcc

HOST_CHRIS_INC := -Icompiler/chrisld -Icompiler/chrisasm -Icompiler/kcc

CFLAGS := -std=c11 -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-fcf-protection=none -mno-red-zone -mcmodel=kernel -mno-mmx -mno-sse -mno-sse2 \
	-DLIMINE_API_REVISION=3 -D__freestanding__ -I$(LIMINE_DIR) $(KINC) \
	-Wall -Wextra -Werror
LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
	-z noexecstack -T kernel/metal/linker.ld

C_OBJECTS_REL := kernel/metal/start.o kernel/metal/port.o kernel/metal/serial.o \
	kernel/metal/string.o \
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
	kernel/tools/shell.o kernel/tools/chrisbuild.o \
	compiler/chrisld/chriso.o compiler/chrisasm/chrisasm.o \
	compiler/chrisld/chrisld.o compiler/kcc/kcc.o \
	kernel/fs/ata_pio.o kernel/fs/cfs.o kernel/fs/cfs_fsck.o \
	kernel/fs/storage.o kernel/fs/fs.o \
	kernel/lang/lang_sys.o kernel/lang/clvm_sys.o \
	compiler/lang_pipeline.o compiler/chrisc/chrisc.o \
	compiler/clvm/clasm.o compiler/clvm/clvm_format.o \
	compiler/clvm/clvm_vm.o kernel/tools/app_window.o \
	compiler/jit/jit.o compiler/jit/jit_emit.o compiler/jit/jit_compile.o \
	kernel/tools/taskmgr.o kernel/metal/apic.o kernel/metal/ioapic.o \
	kernel/metal/spin.o kernel/metal/smp.o kernel/metal/job.o \
	kernel/metal/kcc_job.o kernel/net/net_xfer.o

ASM_OBJECTS_REL := kernel/metal/idt_stubs.o
C_OBJECTS := $(addprefix $(OBJ_DIR)/,$(C_OBJECTS_REL))
ASM_OBJECTS := $(addprefix $(OBJ_DIR)/,$(ASM_OBJECTS_REL))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)

HOST_CC := gcc
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -Ikernel/tools -Ikernel/fs -Itools

HOST_NET_PORT ?= 7007
HOST_XFER_PORT ?= 9016

.PHONY: all iso run run-stop clean disk disk.img host-gates seed-selfhost disk-seed

disk-seed: seed-selfhost

run-stop:
	-killall qemu-system-x86_64 2>/dev/null || pkill -x qemu-system-x86_64 2>/dev/null || true

all: iso

iso: $(ISO)

send:
	@test -n "$(CFS_PATH)" || (echo "usage: make send CFS_PATH=SYS/FILE.C HOST_FILE=foo.c" && exit 1)
	@test -n "$(HOST_FILE)" || (echo "usage: make send CFS_PATH=SYS/FILE.C HOST_FILE=foo.c" && exit 1)
	python3 tools/cfs_send.py $(CFS_PATH) $(HOST_FILE)

host-cfs-test: tools/test_cfs_host.c kernel/fs/cfs.c kernel/fs/cfs.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/test_cfs_host tools/test_cfs_host.c kernel/fs/cfs.c
	$(HOST_BIN)/test_cfs_host

host-cfs-maxwrite-test: tools/test_cfs_maxwrite.c kernel/fs/cfs.c tools/test_disk.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs -Itools \
		-o $(HOST_BIN)/test_cfs_maxwrite tools/test_cfs_maxwrite.c kernel/fs/cfs.c
	$(HOST_BIN)/test_cfs_maxwrite

host-fsck-test: tools/test_cfs_fsck.c kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/test_cfs_fsck tools/test_cfs_fsck.c \
		kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	$(HOST_BIN)/test_cfs_fsck

host-input-test: tools/test_input.c kernel/gfx/input.c kernel/gfx/input.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx \
		-o $(HOST_BIN)/test_input tools/test_input.c kernel/gfx/input.c
	$(HOST_BIN)/test_input

host-editor-test: host/test_editor64.c kernel/tools/editor.c kernel/tools/editor.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -o $(HOST_BIN)/test_editor64 \
		host/test_editor64.c kernel/tools/editor.c
	$(HOST_BIN)/test_editor64

host-cfs-migrate-v2v3: tools/cfs_migrate_v2v3.c kernel/fs/cfs.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/cfs_migrate_v2v3 tools/cfs_migrate_v2v3.c kernel/fs/cfs.c

test_gfx2d: kernel/gfx/gfx2d.c tools/test_gfx2d.c kernel/gfx/gfx2d.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx \
		kernel/gfx/gfx2d.c tools/test_gfx2d.c -o $(HOST_BIN)/test_gfx2d

test_keystate: tools/test_keystate.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror tools/test_keystate.c \
		-o $(HOST_BIN)/test_keystate

host-cfs-paths-test: tools/test_cfs_paths.c kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/test_cfs_paths tools/test_cfs_paths.c \
		kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	$(HOST_BIN)/test_cfs_paths

host-cfs-indirect-test: tools/test_cfs_indirect.c kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/test_cfs_indirect tools/test_cfs_indirect.c \
		kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	$(HOST_BIN)/test_cfs_indirect

host-cfs-journal-test: tools/test_cfs_journal.c kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/test_cfs_journal tools/test_cfs_journal.c \
		kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	$(HOST_BIN)/test_cfs_journal

host-cfs-chmod: tools/cfs_chmod.c kernel/fs/cfs.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/cfs_chmod tools/cfs_chmod.c kernel/fs/cfs.c

host-cfs-chmod-test: tools/test_cfs_chmod.c kernel/fs/cfs.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/test_cfs_chmod tools/test_cfs_chmod.c kernel/fs/cfs.c
	$(HOST_BIN)/test_cfs_chmod

host-cfs-put: tools/cfs_put.c kernel/fs/cfs.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/cfs_put tools/cfs_put.c kernel/fs/cfs.c

host-cfs-mkdisk: tools/cfs_mkdisk.c kernel/fs/cfs.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/cfs_mkdisk tools/cfs_mkdisk.c kernel/fs/cfs.c

disk.img: $(DISK_IMG)

$(DISK_IMG): host-cfs-mkdisk
	@if [ -f $(DISK_IMG) ]; then \
		echo "$(DISK_IMG) exists (use rm $(DISK_IMG) to recreate)"; \
	else \
		$(HOST_BIN)/cfs_mkdisk $(DISK_IMG); \
	fi

$(USER_BIN)/hello.elf: user/hello.asm
	mkdir -p $(USER_BIN)
	nasm -f bin user/hello.asm -o $@

$(USER_BIN)/fault.elf: user/fault.asm
	mkdir -p $(USER_BIN)
	nasm -f bin user/fault.asm -o $@

host-cfs-put-file: tools/cfs_put_file.c kernel/fs/cfs.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/cfs_put_file tools/cfs_put_file.c kernel/fs/cfs.c

disk-hello: $(DISK_IMG) $(USER_BIN)/hello.elf host-cfs-put-file
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) BIN/HELLO.ELF $(USER_BIN)/hello.elf

disk-fault: $(DISK_IMG) $(USER_BIN)/fault.elf host-cfs-put-file
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) BIN/FAULT.ELF $(USER_BIN)/fault.elf

disk: disk-hello disk-fault host-cfs-put
	$(HOST_BIN)/cfs_put $(DISK_IMG)

host-gates: host-cfs-test host-fsck-test host-cfs-paths-test \
	host-cfs-indirect-test host-cfs-journal-test host-cfs-chmod-test \
	host-jit-test host-chriso-test host-chrisasm-test host-chrisld-test \
	host-kcc-test

host-chrisasm-test: tools/test_chrisasm.c compiler/chrisasm/chrisasm.c \
		compiler/chrisld/chriso.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_CHRIS_INC) \
		tools/test_chrisasm.c compiler/chrisasm/chrisasm.c \
		compiler/chrisld/chriso.c -o $(HOST_BIN)/test_chrisasm
	$(HOST_BIN)/test_chrisasm

host-chrisld-test: tools/test_chrisld.c compiler/chrisasm/chrisasm.c \
		compiler/chrisld/chriso.c compiler/chrisld/chrisld.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_CHRIS_INC) \
		tools/test_chrisld.c compiler/chrisasm/chrisasm.c \
		compiler/chrisld/chriso.c compiler/chrisld/chrisld.c \
		-o $(HOST_BIN)/test_chrisld
	$(HOST_BIN)/test_chrisld

host-kcc-test: tools/test_kcc.c compiler/kcc/kcc.c compiler/chrisasm/chrisasm.c \
		compiler/chrisld/chriso.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_CHRIS_INC) \
		tools/test_kcc.c compiler/kcc/kcc.c compiler/chrisasm/chrisasm.c \
		compiler/chrisld/chriso.c -o $(HOST_BIN)/test_kcc
	$(HOST_BIN)/test_kcc

host-kcc: tools/kcc_main.c compiler/kcc/kcc.c compiler/chrisasm/chrisasm.c \
		compiler/chrisld/chriso.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_CHRIS_INC) \
		tools/kcc_main.c compiler/kcc/kcc.c compiler/chrisasm/chrisasm.c \
		compiler/chrisld/chriso.c -o $(HOST_BIN)/kcc

host-seed-selfhost: tools/seed_selfhost.c kernel/fs/cfs.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/seed_selfhost tools/seed_selfhost.c kernel/fs/cfs.c

seed-selfhost: iso $(DISK_IMG) host-kcc host-seed-selfhost
	$(HOST_BIN)/seed_selfhost $(DISK_IMG)

host-cfs-migrate: tools/cfs_migrate_v1v2.c kernel/fs/cfs.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/cfs_migrate_v1v2 tools/cfs_migrate_v1v2.c kernel/fs/cfs.c

host-chriso-test: tools/test_chriso.c compiler/chrisld/chriso.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisld \
		tools/test_chriso.c compiler/chrisld/chriso.c -o $(HOST_BIN)/test_chriso
	$(HOST_BIN)/test_chriso

host-jit-test: tools/test_jit_enc.c tools/jit_host_stub.c compiler/jit/jit_emit.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/jit \
		tools/jit_host_stub.c compiler/jit/jit_emit.c tools/test_jit_enc.c \
		-o $(HOST_BIN)/test_jit_enc
	$(HOST_BIN)/test_jit_enc

disk-put: host-cfs-put-file run-stop
	@test -n "$(CFS_PATH)" || (echo "usage: make disk-put CFS_PATH=SYS/FILE.C HOST_FILE=foo.c" && exit 1)
	@test -n "$(HOST_FILE)" || (echo "usage: make disk-put CFS_PATH=SYS/FILE.C HOST_FILE=foo.c" && exit 1)
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) $(CFS_PATH) $(HOST_FILE)

$(OBJ_DIR)/kernel/metal/%.o: kernel/metal/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/%.o: kernel/gfx/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/wm/%.o: kernel/wm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/tools/%.o: kernel/tools/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/fs/%.o: kernel/fs/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/lang/%.o: kernel/lang/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/net/%.o: kernel/net/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/%.o: compiler/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/chrisc/%.o: compiler/chrisc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/clvm/%.o: compiler/clvm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/jit/%.o: compiler/jit/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/chrisld/%.o: compiler/chrisld/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/chrisasm/%.o: compiler/chrisasm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/kcc/%.o: compiler/kcc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/metal/%.o: kernel/metal/%.asm
	@mkdir -p $(dir $@)
	nasm -f elf64 $< -o $@

$(KERNEL): $(OBJECTS) kernel/metal/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(ISO_ROOT)/boot/limine/limine.conf: $(LIMINE_CONF_SRC)
	@mkdir -p $(dir $@)
	cp $< $@

$(ISO_ROOT)/boot/limine/limine-bios-cd.bin: $(LIMINE_DIR)/limine-bios-cd.bin
	@mkdir -p $(dir $@)
	cp $< $@

$(ISO_ROOT)/boot/limine/limine-bios.sys: $(LIMINE_DIR)/limine-bios.sys
	@mkdir -p $(dir $@)
	cp $< $@

$(ISO_ROOT)/boot/limine/limine-uefi-cd.bin: $(LIMINE_DIR)/limine-uefi-cd.bin
	@mkdir -p $(dir $@)
	cp $< $@

$(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI: $(LIMINE_DIR)/BOOTX64.EFI
	@mkdir -p $(dir $@)
	cp $< $@

$(ISO): $(KERNEL) $(ISO_ROOT)/boot/limine/limine.conf \
	$(ISO_ROOT)/boot/limine/limine-bios-cd.bin \
	$(ISO_ROOT)/boot/limine/limine-bios.sys \
	$(ISO_ROOT)/boot/limine/limine-uefi-cd.bin \
	$(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	@mkdir -p $(dir $@)
	$(XORRISO) -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image \
		--protective-msdos-label $(ISO_ROOT) -o $@
	$(LIMINE_DIR)/limine bios-install $@

run: $(ISO) run-stop
	@test -f $(DISK_IMG) || $(MAKE) disk.img
	@sleep 1
	$(QEMU) -M pc -m 1G -smp 2 -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0 \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-device virtio-net-pci,netdev=n0 \
		-netdev user,id=n0,hostfwd=udp:127.0.0.1:$(HOST_NET_PORT)-:7,hostfwd=tcp:127.0.0.1:$(HOST_NET_PORT)-:7,hostfwd=tcp:127.0.0.1:$(HOST_XFER_PORT)-:9016 \
		-serial stdio -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD_DIR)
	rm -f kernel/metal/*.o kernel/gfx/*.o kernel/wm/*.o kernel/tools/*.o \
		kernel/fs/*.o kernel/lang/*.o kernel/net/*.o compiler/*.o \
		compiler/chrisc/*.o compiler/clvm/*.o compiler/jit/*.o \
		compiler/chrisld/*.o compiler/chrisasm/*.o compiler/kcc/*.o \
		os.iso disk.img disk_v2.img
