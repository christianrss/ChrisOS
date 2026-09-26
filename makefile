CC := gcc
LD := ld
QEMU := qemu-system-x86_64
# zoom-to-fit: guest 1080p cabe no monitor; grab-on-hover: mouse sem clique preciso.
# Clicar na janela do QEMU (grab-on-hover) para o guest receber teclas.
# Windows sem GTK: make run QEMU_DISPLAY=sdl,grab-mod=lshift-lshift
QEMU_DISPLAY ?= gtk,zoom-to-fit=on,grab-on-hover=on,show-cursor=on
QEMU_MEM ?= 16G
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
	-Ikernel/fs -Ikernel/lang -Ikernel/net -Ikernel/crypto -Icompiler -Icompiler/clvm \
	-Icompiler/jit -Icompiler/chrisld -Icompiler/chrisasm -Icompiler/kcc

HOST_CHRIS_INC := -Icompiler/chrisld -Icompiler/chrisasm -Icompiler/kcc

CHRIS_GIT := $(shell git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
CHRIS_DATE := $(shell date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || echo unknown)
CFLAGS := -std=c11 -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-fcf-protection=none -mno-red-zone -mcmodel=kernel -mno-mmx -mno-sse -mno-sse2 \
	-DLIMINE_API_REVISION=3 -D__freestanding__ -I$(LIMINE_DIR) $(KINC) \
	-DCHRIS_GIT=\"$(CHRIS_GIT)\" -DCHRIS_BUILD_ID=\"$(CHRIS_GIT)-$(CHRIS_DATE)\" \
	-DCHRIS_DATE=\"$(CHRIS_DATE)\" \
	-Wall -Wextra -Werror
LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
	-z noexecstack -T kernel/metal/linker.ld

GFX_3D_OBJS := \
	kernel/gfx/sse_init.o \
	kernel/gfx/gfx_fast.o \
	kernel/gfx/math3d.o \
	kernel/gfx/zbuf.o \
	kernel/gfx/tri.o \
	kernel/gfx/mesh.o \
	kernel/gfx/tile.o \
	kernel/gfx/bench.o \
	kernel/gfx/tri_bin.o \
	kernel/gfx/gfx_slot.o \
	kernel/gfx/shade.o \
	kernel/gfx/tex.o \
	kernel/gfx/voxel.o \
	kernel/gfx/gfx3d_ctx.o \
	kernel/gfx/scene.o \
	kernel/gfx/phys.o

# kernel C usa -mno-sse; gfx 3D compila com SSE2 + FPU XMM (fxsave nos ISRs).
GFX_CFLAGS_BASE := $(filter-out -mno-mmx -mno-sse -mno-sse2,$(CFLAGS))
GFX_FLOAT_CFLAGS := $(GFX_CFLAGS_BASE) -msse2 -mfpmath=sse -ffast-math
GFX_SSE2_CFLAGS := $(GFX_CFLAGS_BASE) -msse2

C_OBJECTS_REL := kernel/metal/start.o kernel/metal/port.o kernel/metal/serial.o \
	kernel/metal/string.o \
	kernel/metal/panic.o kernel/metal/gdt.o kernel/metal/idt.o \
	kernel/metal/irq.o kernel/metal/syscall.o kernel/metal/user_enter.o \
	kernel/metal/elf.o kernel/metal/pit.o kernel/metal/ps2.o \
	kernel/metal/bootinfo.o kernel/metal/pmm.o kernel/metal/mm.o \
	kernel/metal/tlb_proto.o kernel/metal/klog.o kernel/metal/buildid.o \
	kernel/metal/meminfo.o \
	kernel/metal/heap.o kernel/metal/pci.o \
	kernel/net/virtio_net.o kernel/net/net.o kernel/net/sock.o \
	kernel/crypto/sha256.o kernel/crypto/rng.o kernel/crypto/aes.o kernel/crypto/x25519.o \
	kernel/gfx/ac97.o kernel/gfx/hwgate.o \
	kernel/gfx/virtq.o kernel/gfx/gpures.o kernel/gfx/virtgpu_enc.o \
	kernel/gfx/virgl_cmd.o kernel/gfx/vgpu.o kernel/gfx/virgl_demo.o \
	kernel/gfx/shader/sh_lex.o kernel/gfx/shader/sh_parse.o kernel/gfx/shader/sh_sem.o \
	kernel/gfx/shader/sh_ir.o kernel/gfx/shader/sh_tgsi.o kernel/gfx/shader/sh_exec.o \
	kernel/gfx/shader/sh_api.o \
	kernel/gfx/graphics.o kernel/gfx/font.o kernel/gfx/icons_tab.o kernel/gfx/icons_bin.o \
	kernel/gfx/input.o \
	kernel/gfx/speaker.o kernel/gfx/gfx2d.o \
	kernel/wm/task.o kernel/wm/ui.o kernel/wm/desktop.o kernel/wm/main.o \
	kernel/wm/boot_splash.o \
	kernel/tools/editor.o kernel/tools/editor_window.o kernel/tools/explorer.o \
	kernel/tools/shell.o kernel/tools/chrisbuild.o kernel/tools/chrismake.o kernel/tools/native_link.o \
	compiler/chrisld/chriso.o compiler/chrisasm/chrisasm.o \
	compiler/chrisld/chrisld.o compiler/kcc/kcc.o \
	kernel/fs/ata_pio.o kernel/fs/cfs.o kernel/fs/cfs_fsck.o \
	kernel/fs/storage.o kernel/fs/fs.o kernel/fs/bdev.o kernel/fs/part.o \
	kernel/fs/ahci.o kernel/fs/nvme.o kernel/fs/virtio_blk.o \
	kernel/fs/usb_msc.o kernel/fs/xhci.o kernel/fs/install.o kernel/metal/acpi.o \
	kernel/lang/lang_sys.o kernel/lang/clvm_sys.o \
	compiler/lang_pipeline.o compiler/debug/cdbg.o compiler/debug/dbg_session.o \
	compiler/chrisc/chrisc.o \
	compiler/clvm/clasm.o compiler/clvm/clvm_format.o \
	compiler/clvm/clvm_vm.o kernel/tools/app_window.o \
	compiler/jit/jit.o compiler/jit/jit_emit.o compiler/jit/jit_compile.o \
	compiler/jit/jit_runtime.o \
	compiler/gc/gc.o compiler/il/il.o compiler/cla/cla.o compiler/cls/cls.o \
	kernel/tools/taskmgr.o kernel/metal/apic.o kernel/metal/ioapic.o \
	kernel/metal/spin.o kernel/metal/smp.o kernel/metal/job.o \
	kernel/metal/kthread.o kernel/metal/kcc_job.o kernel/metal/proc.o kernel/net/net_xfer.o \
	$(GFX_3D_OBJS)

ASM_OBJECTS_REL := kernel/metal/idt_stubs.o
C_OBJECTS := $(addprefix $(OBJ_DIR)/,$(C_OBJECTS_REL))
ASM_OBJECTS := $(addprefix $(OBJ_DIR)/,$(ASM_OBJECTS_REL))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)

HOST_CC := gcc
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -Ikernel/tools -Ikernel/fs -Itools

HOST_NET_PORT ?= 7007
HOST_XFER_PORT ?= 9016

$(OBJ_DIR)/kernel/gfx/sse_init.o: kernel/gfx/sse_init.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/gfx_fast.o: kernel/gfx/gfx_fast.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_SSE2_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/math3d.o: kernel/gfx/math3d.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_FLOAT_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/mesh.o: kernel/gfx/mesh.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_FLOAT_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/zbuf.o: kernel/gfx/zbuf.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_SSE2_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/tri.o: kernel/gfx/tri.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_SSE2_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/tile.o: kernel/gfx/tile.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_SSE2_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/bench.o: kernel/gfx/bench.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_SSE2_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/tri_bin.o: kernel/gfx/tri_bin.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_SSE2_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/gfx_slot.o: kernel/gfx/gfx_slot.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_SSE2_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/shade.o: kernel/gfx/shade.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_FLOAT_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/tex.o: kernel/gfx/tex.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_SSE2_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/voxel.o: kernel/gfx/voxel.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_FLOAT_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/gfx3d_ctx.o: kernel/gfx/gfx3d_ctx.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_FLOAT_CFLAGS) -c $< -o $@

kernel/gfx/shader/sh_src.h: tools/embed_glsl.py $(wildcard kernel/gfx/shader/glsl/*.vert) $(wildcard kernel/gfx/shader/glsl/*.frag)
	python3 tools/embed_glsl.py kernel/gfx/shader/sh_src.h

$(OBJ_DIR)/kernel/gfx/shader/%.o: kernel/gfx/shader/%.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_FLOAT_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/gfx/virgl_demo.o: kernel/gfx/virgl_demo.c kernel/gfx/shader/sh_src.h
	@mkdir -p $(dir $@)
	$(CC) $(GFX_FLOAT_CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/lang/clvm_sys.o: kernel/lang/clvm_sys.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_FLOAT_CFLAGS) -c $< -o $@


.PHONY: all iso run run-stop clean disk disk.img host-gates seed-selfhost disk-seed kernel apps chrisvm chrisvm-test

disk-seed: seed-selfhost

run-stop:
	-killall qemu-system-x86_64 2>/dev/null || pkill -x qemu-system-x86_64 2>/dev/null || true

all: iso

iso: $(ISO)

kernel: $(KERNEL)

apps: disk-base

disk-base: disk-ui $(KERNEL) $(LIMINE_DIR)/BOOTX64.EFI
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/Makefile APPS/Makefile
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/CC/CC.CC APPS/CC/CC.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/CC/HELLO.CC APPS/CC/HELLO.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/CC/IN.CC APPS/CC/IN.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/CC/OUT.CLV APPS/CC/OUT.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/THREADS/COUNT.CC APPS/THREADS/COUNT.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/NET/HTTP.CC APPS/NET/HTTP.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/NET/FTP.CC APPS/NET/FTP.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/NET/MAIL.CC APPS/NET/MAIL.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/NET/BROWSER.CC APPS/NET/BROWSER.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/NET/TLS.CC APPS/NET/TLS.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/NET/SSH.CC APPS/NET/SSH.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/WAV/PLAY.CC APPS/WAV/PLAY.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/PAINT/PAINT.CC APPS/PAINT/PAINT.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/BMP.H LIB/BMP.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/BMP.CC LIB/BMP.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/AC97.CC SYS/DRV/AC97.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/CC/CC.CLV APPS/CC/CC.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/CC/DOCC APPS/CC/DOCC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/CC/STRUCT.CC APPS/CC/STRUCT.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/CC/FIELDS.CC APPS/CC/FIELDS.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) BOOT/KERNEL.ELF $(KERNEL)
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) EFI/BOOT/BOOTX64.EFI $(LIMINE_DIR)/BOOTX64.EFI
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) BOOT/LIMINE.CFG iso_root/boot/limine/limine.conf
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/THREADS/COUNT.CLV APPS/THREADS/COUNT.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/NET/HTTP.CLV APPS/NET/HTTP.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/NET/FTP.CLV APPS/NET/FTP.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/NET/MAIL.CLV APPS/NET/MAIL.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/NET/BROWSER.CLV APPS/NET/BROWSER.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/NET/TLS.CLV APPS/NET/TLS.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/NET/SSH.CLV APPS/NET/SSH.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/WAV/PLAY.CLV APPS/WAV/PLAY.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/PAINT/PAINT.CLV APPS/PAINT/PAINT.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/AC97.CLV SYS/DRV/AC97.CLV

test_sse_init: tools/test_sse_init.c kernel/gfx/sse_init.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx \
		tools/test_sse_init.c kernel/gfx/sse_init.c -o $(HOST_BIN)/test_sse_init
	$(HOST_BIN)/test_sse_init

test_math3d: tools/test_math3d.c kernel/gfx/math3d.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx -ffast-math \
		tools/test_math3d.c kernel/gfx/math3d.c -lm -o $(HOST_BIN)/test_math3d
	$(HOST_BIN)/test_math3d

test_math3d_view: tools/test_math3d_view.c kernel/gfx/math3d.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx -ffast-math \
		tools/test_math3d_view.c kernel/gfx/math3d.c -lm -o $(HOST_BIN)/test_math3d_view
	$(HOST_BIN)/test_math3d_view

test_zbuf: tools/test_zbuf.c kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx -msse2 \
		tools/test_zbuf.c kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c \
		-o $(HOST_BIN)/test_zbuf
	$(HOST_BIN)/test_zbuf

test_mesh: tools/test_mesh.c kernel/gfx/mesh.c kernel/gfx/math3d.c kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c kernel/gfx/tri.c kernel/gfx/gfx2d.c kernel/gfx/shade.c kernel/gfx/tex.c compiler/clvm/clvm_vm.c compiler/clvm/clvm_format.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx -Icompiler/clvm -Icompiler \
		-ffast-math -msse2 -mfpmath=sse tools/test_mesh.c kernel/gfx/mesh.c kernel/gfx/math3d.c \
		kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c kernel/gfx/tri.c kernel/gfx/gfx2d.c \
		kernel/gfx/shade.c kernel/gfx/tex.c \
		compiler/clvm/clvm_vm.c compiler/clvm/clvm_format.c \
		-o $(HOST_BIN)/test_mesh
	$(HOST_BIN)/test_mesh

test_cube_mesh: tools/test_cube_mesh.c kernel/gfx/mesh.c kernel/gfx/math3d.c kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c kernel/gfx/tri.c kernel/gfx/gfx2d.c kernel/gfx/shade.c kernel/gfx/tex.c compiler/clvm/clvm_vm.c compiler/clvm/clvm_format.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx -Icompiler/clvm -Icompiler \
		-ffast-math -msse2 -mfpmath=sse tools/test_cube_mesh.c kernel/gfx/mesh.c kernel/gfx/math3d.c \
		kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c kernel/gfx/tri.c kernel/gfx/gfx2d.c \
		kernel/gfx/shade.c kernel/gfx/tex.c \
		compiler/clvm/clvm_vm.c compiler/clvm/clvm_format.c \
		-o $(HOST_BIN)/test_cube_mesh
	$(HOST_BIN)/test_cube_mesh

test_cube_mesh_f: tools/test_cube_mesh_f.c kernel/gfx/mesh.c kernel/gfx/math3d.c kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c kernel/gfx/tri.c kernel/gfx/gfx2d.c kernel/gfx/shade.c kernel/gfx/tex.c compiler/clvm/clvm_vm.c compiler/clvm/clvm_format.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx -Icompiler/clvm -Icompiler \
		-ffast-math -msse2 -mfpmath=sse tools/test_cube_mesh_f.c kernel/gfx/mesh.c kernel/gfx/math3d.c \
		kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c kernel/gfx/tri.c kernel/gfx/gfx2d.c \
		kernel/gfx/shade.c kernel/gfx/tex.c \
		compiler/clvm/clvm_vm.c compiler/clvm/clvm_format.c \
		-o $(HOST_BIN)/test_cube_mesh_f
	$(HOST_BIN)/test_cube_mesh_f

test_chunk_mesh: tools/test_chunk_mesh.c kernel/gfx/voxel.c kernel/gfx/math3d.c kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c kernel/gfx/tri.c kernel/gfx/gfx2d.c kernel/gfx/shade.c kernel/gfx/tex.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx -Icompiler/clvm -Icompiler \
		-ffast-math -msse2 -mfpmath=sse tools/test_chunk_mesh.c kernel/gfx/voxel.c \
		kernel/gfx/math3d.c kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c kernel/gfx/tri.c \
		kernel/gfx/gfx2d.c kernel/gfx/shade.c kernel/gfx/tex.c \
		-o $(HOST_BIN)/test_chunk_mesh
	$(HOST_BIN)/test_chunk_mesh

test_chrisc_arrays: tools/test_chrisc_arrays.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_arrays.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-o $(HOST_BIN)/test_chrisc_arrays
	$(HOST_BIN)/test_chrisc_arrays

test_chrisc_float: tools/test_chrisc_float.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_float.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-o $(HOST_BIN)/test_chrisc_float
	$(HOST_BIN)/test_chrisc_float

test_chrisc_ptr_float: tools/test_chrisc_ptr_float.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_ptr_float.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-lm -o $(HOST_BIN)/test_chrisc_ptr_float
	$(HOST_BIN)/test_chrisc_ptr_float

test_chrisc_move: tools/test_chrisc_move.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_move.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-lm -o $(HOST_BIN)/test_chrisc_move
	$(HOST_BIN)/test_chrisc_move

test_chrisc_fn: tools/test_chrisc_fn.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_fn.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-o $(HOST_BIN)/test_chrisc_fn
	$(HOST_BIN)/test_chrisc_fn

test_chrisc_struct: tools/test_chrisc_struct.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_struct.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-o $(HOST_BIN)/test_chrisc_struct
	$(HOST_BIN)/test_chrisc_struct

test_chrisc_games: tools/test_chrisc_games.c compiler/chrisc/chrisc.c compiler/clvm/clasm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_games.c compiler/chrisc/chrisc.c compiler/clvm/clasm.c \
		-o $(HOST_BIN)/test_chrisc_games
	$(HOST_BIN)/test_chrisc_games

test_chrisc_include: tools/test_chrisc_include.c compiler/chrisc/chrisc.c compiler/clvm/clasm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_include.c compiler/chrisc/chrisc.c compiler/clvm/clasm.c \
		-o $(HOST_BIN)/test_chrisc_include
	$(HOST_BIN)/test_chrisc_include

test_chrisc_apps: tools/test_chrisc_apps.c compiler/chrisc/chrisc.c compiler/clvm/clasm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_apps.c compiler/chrisc/chrisc.c compiler/clvm/clasm.c \
		-o $(HOST_BIN)/test_chrisc_apps
	$(HOST_BIN)/test_chrisc_apps

test_editor_vi: tools/test_editor_vi.c tools/jit_host_stub.c compiler/jit/jit_emit.c \
		compiler/jit/jit_compile.c compiler/jit/jit_runtime.c \
		compiler/chrisc/chrisc.c 		compiler/clvm/clasm.c compiler/clvm/clvm_format.c \
		compiler/clvm/clvm_vm.c kernel/metal/spin.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/jit -Icompiler/chrisc \
		-Icompiler/clvm -Icompiler -Ikernel/lang -Ikernel/metal \
		tools/jit_host_stub.c compiler/jit/jit_emit.c compiler/jit/jit_compile.c \
		compiler/jit/jit_runtime.c tools/test_editor_vi.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		kernel/metal/spin.c \
		-o $(HOST_BIN)/test_editor_vi
	$(HOST_BIN)/test_editor_vi

test_chrisc_string: tools/test_chrisc_string.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_string.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-o $(HOST_BIN)/test_chrisc_string
	$(HOST_BIN)/test_chrisc_string

test_chrisc_ptrwidth: tools/test_chrisc_ptrwidth.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		LIB/STRING.CC
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_ptrwidth.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-o $(HOST_BIN)/test_chrisc_ptrwidth
	$(HOST_BIN)/test_chrisc_ptrwidth

test_editor_path: tools/test_editor_path.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		APPS/EDITOR/EDITOR.CC
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_editor_path.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-o $(HOST_BIN)/test_editor_path
	$(HOST_BIN)/test_editor_path

test_cdbg: tools/test_cdbg.c compiler/debug/cdbg.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler -Icompiler/chrisc -Icompiler/clvm \
		tools/test_cdbg.c compiler/debug/cdbg.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c \
		-o $(HOST_BIN)/test_cdbg
	$(HOST_BIN)/test_cdbg

test_dbg_step: tools/test_dbg_step.c compiler/debug/dbg_session.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler \
		tools/test_dbg_step.c compiler/debug/dbg_session.c \
		-o $(HOST_BIN)/test_dbg_step
	$(HOST_BIN)/test_dbg_step

test_editmodel: tools/test_editmodel.c compiler/edit/editmodel.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler \
		tools/test_editmodel.c compiler/edit/editmodel.c \
		-o $(HOST_BIN)/test_editmodel
	$(HOST_BIN)/test_editmodel

test_chrisc_lang: tools/test_chrisc_lang.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_lang.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-o $(HOST_BIN)/test_chrisc_lang
	$(HOST_BIN)/test_chrisc_lang

test_chrisc_c17: tools/test_chrisc_c17.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_c17.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-o $(HOST_BIN)/test_chrisc_c17
	$(HOST_BIN)/test_chrisc_c17

test_chrisc_doom: tools/test_chrisc_doom.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_chrisc_doom.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-o $(HOST_BIN)/test_chrisc_doom
	$(HOST_BIN)/test_chrisc_doom

test_cla_gc: tools/test_cla_gc.c compiler/cla/cla.c compiler/gc/gc.c compiler/il/il.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler -Icompiler/clvm \
		tools/test_cla_gc.c compiler/cla/cla.c compiler/gc/gc.c compiler/il/il.c \
		-o $(HOST_BIN)/test_cla_gc
	$(HOST_BIN)/test_cla_gc

test_chrisc_trig: tools/test_chrisc_trig.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		kernel/gfx/math3d.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		-Ikernel/gfx -msse2 -mfpmath=sse -ffast-math \
		tools/test_chrisc_trig.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		kernel/gfx/math3d.c -lm \
		-o $(HOST_BIN)/test_chrisc_trig
	$(HOST_BIN)/test_chrisc_trig

test_tri: kernel/gfx/gfx2d.c tools/test_tri.c kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c kernel/gfx/tri.c kernel/gfx/shade.c kernel/gfx/tex.c kernel/gfx/math3d.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx -msse2 -ffast-math \
		kernel/gfx/gfx2d.c kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c kernel/gfx/tri.c \
		kernel/gfx/shade.c kernel/gfx/tex.c kernel/gfx/math3d.c \
		tools/test_tri.c -o $(HOST_BIN)/test_tri
	$(HOST_BIN)/test_tri

test_tile: tools/test_tile.c kernel/gfx/tile.c tools/job_host_stub.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx -Ikernel/metal -Itools -msse2 -pthread \
		-ffast-math tools/test_tile.c kernel/gfx/tile.c kernel/gfx/tri.c kernel/gfx/zbuf.c \
		kernel/gfx/gfx_fast.c kernel/gfx/gfx2d.c kernel/gfx/tri_bin.c kernel/gfx/shade.c \
		kernel/gfx/tex.c kernel/gfx/math3d.c tools/job_host_stub.c \
		-o $(HOST_BIN)/test_tile
	$(HOST_BIN)/test_tile

test_tile_bin: tools/test_tile_bin.c kernel/gfx/tri_bin.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx \
		tools/test_tile_bin.c kernel/gfx/tri_bin.c -o $(HOST_BIN)/test_tile_bin
	$(HOST_BIN)/test_tile_bin

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

host-pmm-heap-smp-test: tools/test_pmm_heap_smp.c tools/host_metal/metal_stub.c \
		kernel/metal/pmm.c kernel/metal/heap.c kernel/metal/spin.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -DCHRIS_HOST_METAL -pthread \
		-Itools/host_metal -Ikernel/metal \
		-o $(HOST_BIN)/test_pmm_heap_smp tools/test_pmm_heap_smp.c \
		tools/host_metal/metal_stub.c kernel/metal/pmm.c kernel/metal/heap.c \
		kernel/metal/spin.c
	$(HOST_BIN)/test_pmm_heap_smp

host-kthread-smp-test: tools/test_kthread_smp.c tools/host_metal/metal_stub.c \
		kernel/metal/kthread.c kernel/metal/job.c kernel/metal/spin.c \
		kernel/metal/tlb_proto.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -DCHRIS_HOST_METAL -pthread \
		-Ikernel/metal \
		-o $(HOST_BIN)/test_kthread_smp tools/test_kthread_smp.c \
		tools/host_metal/metal_stub.c kernel/metal/kthread.c kernel/metal/job.c \
		kernel/metal/spin.c kernel/metal/tlb_proto.c
	$(HOST_BIN)/test_kthread_smp

host-job-saturate-test: tools/test_job_saturate.c kernel/metal/job.c kernel/metal/spin.c \
		kernel/metal/tlb_proto.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/metal \
		-o $(HOST_BIN)/test_job_saturate tools/test_job_saturate.c \
		kernel/metal/job.c kernel/metal/spin.c kernel/metal/tlb_proto.c
	$(HOST_BIN)/test_job_saturate

host-klog-test: tools/test_klog.c kernel/metal/klog.c kernel/metal/spin.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/metal \
		-o $(HOST_BIN)/test_klog tools/test_klog.c kernel/metal/klog.c kernel/metal/spin.c
	$(HOST_BIN)/test_klog

host-buildinfo-test: tools/test_buildinfo.c kernel/metal/buildid.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/metal \
		-DCHRIS_GIT=\"abc\" -DCHRIS_BUILD_ID=\"abc-2026\" -DCHRIS_DATE=\"2026-09-25\" \
		-o $(HOST_BIN)/test_buildinfo tools/test_buildinfo.c kernel/metal/buildid.c
	$(HOST_BIN)/test_buildinfo

host-buildstamp-test: tools/test_buildstamp.c tools/buildstamp.c kernel/crypto/sha256.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/crypto \
		-o $(HOST_BIN)/test_buildstamp tools/test_buildstamp.c tools/buildstamp.c \
		kernel/crypto/sha256.c
	$(HOST_BIN)/test_buildstamp

host-meminfo-test: tools/test_meminfo.c kernel/metal/meminfo.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/metal \
		-o $(HOST_BIN)/test_meminfo tools/test_meminfo.c kernel/metal/meminfo.c
	$(HOST_BIN)/test_meminfo

host-pmm-cycle-test: tools/test_pmm_cycle.c tools/host_metal/metal_stub.c kernel/metal/pmm.c \
		kernel/metal/spin.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -DCHRIS_HOST_METAL \
		-Itools/host_metal -Ikernel/metal \
		-o $(HOST_BIN)/test_pmm_cycle tools/test_pmm_cycle.c \
		tools/host_metal/metal_stub.c kernel/metal/pmm.c kernel/metal/spin.c
	$(HOST_BIN)/test_pmm_cycle

host-tlb-proto-test: tools/test_tlb_proto.c kernel/metal/tlb_proto.c kernel/metal/tlb_proto.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/metal \
		-o $(HOST_BIN)/test_tlb_proto tools/test_tlb_proto.c kernel/metal/tlb_proto.c
	$(HOST_BIN)/test_tlb_proto

host-clvm-sync-test: tools/test_clvm_sync.c kernel/lang/clvm_sync.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/lang \
		-o $(HOST_BIN)/test_clvm_sync tools/test_clvm_sync.c
	$(HOST_BIN)/test_clvm_sync

host-sock-owner-test: tools/test_sock_owner.c kernel/net/sock.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/net -Ikernel/metal \
		-o $(HOST_BIN)/test_sock_owner tools/test_sock_owner.c kernel/net/sock.c
	$(HOST_BIN)/test_sock_owner

host-cfs-lock-test: tools/test_cfs_lock.c kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -pthread -Ikernel/fs -Itools \
		-o $(HOST_BIN)/test_cfs_lock tools/test_cfs_lock.c \
		kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	$(HOST_BIN)/test_cfs_lock

host-gfx3d-ctx-test: tools/test_gfx3d_ctx.c kernel/gfx/gfx3d_ctx.c \
		kernel/gfx/math3d.c kernel/gfx/shade.c kernel/gfx/tex.c kernel/gfx/voxel.c \
		kernel/gfx/zbuf.c kernel/gfx/gfx_fast.c kernel/gfx/tri.c kernel/gfx/gfx2d.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx -Icompiler/clvm -Icompiler \
		-ffast-math -msse2 -mfpmath=sse \
		-o $(HOST_BIN)/test_gfx3d_ctx tools/test_gfx3d_ctx.c \
		kernel/gfx/gfx3d_ctx.c kernel/gfx/math3d.c kernel/gfx/shade.c \
		kernel/gfx/tex.c kernel/gfx/voxel.c kernel/gfx/zbuf.c \
		kernel/gfx/gfx_fast.c kernel/gfx/tri.c kernel/gfx/gfx2d.c -lm
	$(HOST_BIN)/test_gfx3d_ctx

host-sys-write-test: tools/test_sys_write.c kernel/metal/syscall.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
		-Ikernel/metal -o $(HOST_BIN)/test_sys_write tools/test_sys_write.c
	$(HOST_BIN)/test_sys_write

host-fuzz-cfs-test: tools/test_fuzz_cfs.c kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/fs -Itools \
		-o $(HOST_BIN)/test_fuzz_cfs tools/test_fuzz_cfs.c \
		kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	$(HOST_BIN)/test_fuzz_cfs

host-chrisc-read-diag-test: tools/test_chrisc_read_diag.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		-o $(HOST_BIN)/test_chrisc_read_diag tools/test_chrisc_read_diag.c \
		compiler/chrisc/chrisc.c compiler/clvm/clasm.c
	$(HOST_BIN)/test_chrisc_read_diag

host-fuzz-chrisc-test: tools/test_fuzz_chrisc.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		-o $(HOST_BIN)/test_fuzz_chrisc tools/test_fuzz_chrisc.c \
		compiler/chrisc/chrisc.c compiler/clvm/clasm.c
	$(HOST_BIN)/test_fuzz_chrisc

host-fuzz-clvm-test: tools/test_fuzz_clvm.c compiler/clvm/clvm_format.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/clvm \
		-o $(HOST_BIN)/test_fuzz_clvm tools/test_fuzz_clvm.c \
		compiler/clvm/clvm_format.c
	$(HOST_BIN)/test_fuzz_clvm

host-fuzz-elf-test: tools/test_fuzz_elf.c kernel/metal/elf.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/metal \
		-o $(HOST_BIN)/test_fuzz_elf tools/test_fuzz_elf.c kernel/metal/elf.c
	$(HOST_BIN)/test_fuzz_elf

host-elf-malformed-test: tools/test_elf_malformed.c kernel/metal/elf.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/metal \
		-o $(HOST_BIN)/test_elf_malformed tools/test_elf_malformed.c \
		kernel/metal/elf.c
	$(HOST_BIN)/test_elf_malformed

host-editor-test: host/test_editor64.c kernel/tools/editor.c kernel/tools/editor.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -o $(HOST_BIN)/test_editor64 \
		host/test_editor64.c kernel/tools/editor.c
	$(HOST_BIN)/test_editor64

host-graphics-present-test: tools/test_graphics_present.c kernel/gfx/graphics.c \
		kernel/gfx/graphics.h kernel/gfx/gfx_fast.c kernel/gfx/gfx_fast.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/gfx -Ikernel/metal -msse2 \
		-o $(HOST_BIN)/test_graphics_present tools/test_graphics_present.c \
		kernel/gfx/graphics.c kernel/gfx/gfx_fast.c
	$(HOST_BIN)/test_graphics_present

host-task-window-test: tools/test_task_window.c kernel/wm/task.c kernel/wm/task.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/gfx -Ikernel/wm \
		-o $(HOST_BIN)/test_task_window tools/test_task_window.c kernel/wm/task.c
	$(HOST_BIN)/test_task_window

host-slot-front-test: tools/test_slot_front.c kernel/gfx/gfx_fast.c kernel/gfx/gfx_fast.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/gfx -msse2 \
		-o $(HOST_BIN)/test_slot_front tools/test_slot_front.c kernel/gfx/gfx_fast.c
	$(HOST_BIN)/test_slot_front

host-cfs-migrate-v2v3: tools/cfs_migrate_v2v3.c kernel/fs/cfs.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/cfs_migrate_v2v3 tools/cfs_migrate_v2v3.c kernel/fs/cfs.c

host-virtq-test: tools/test_virtq.c kernel/gfx/virtq.c kernel/gfx/virtq.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx \
		tools/test_virtq.c kernel/gfx/virtq.c -o $(HOST_BIN)/test_virtq
	$(HOST_BIN)/test_virtq

host-gpures-test: tools/test_gpures.c kernel/gfx/gpures.c kernel/gfx/gpures.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx \
		tools/test_gpures.c kernel/gfx/gpures.c -o $(HOST_BIN)/test_gpures
	$(HOST_BIN)/test_gpures

host-shader-test: tools/test_shader.c kernel/gfx/shader/sh_lex.c kernel/gfx/shader/sh_parse.c \
		kernel/gfx/shader/sh_sem.c kernel/gfx/shader/sh_ir.c kernel/gfx/shader/sh_tgsi.c \
		kernel/gfx/shader/sh_exec.c kernel/gfx/shader/sh_api.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -DSH_HOST -Ikernel/gfx -msse2 \
		tools/test_shader.c kernel/gfx/shader/sh_lex.c kernel/gfx/shader/sh_parse.c \
		kernel/gfx/shader/sh_sem.c kernel/gfx/shader/sh_ir.c kernel/gfx/shader/sh_tgsi.c \
		kernel/gfx/shader/sh_exec.c kernel/gfx/shader/sh_api.c \
		-o $(HOST_BIN)/test_shader
	$(HOST_BIN)/test_shader

cshader: tools/cshader.c kernel/gfx/shader/sh_lex.c kernel/gfx/shader/sh_parse.c \
		kernel/gfx/shader/sh_sem.c kernel/gfx/shader/sh_ir.c kernel/gfx/shader/sh_tgsi.c \
		kernel/gfx/shader/sh_exec.c kernel/gfx/shader/sh_api.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -DSH_HOST -Ikernel/gfx -msse2 \
		tools/cshader.c kernel/gfx/shader/sh_lex.c kernel/gfx/shader/sh_parse.c \
		kernel/gfx/shader/sh_sem.c kernel/gfx/shader/sh_ir.c kernel/gfx/shader/sh_tgsi.c \
		kernel/gfx/shader/sh_exec.c kernel/gfx/shader/sh_api.c \
		-o $(HOST_BIN)/cshader

host-virgl-cmd-test: tools/test_virgl_cmd.c kernel/gfx/virgl_cmd.c kernel/gfx/virtgpu_enc.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx \
		tools/test_virgl_cmd.c kernel/gfx/virgl_cmd.c kernel/gfx/virtgpu_enc.c \
		-o $(HOST_BIN)/test_virgl_cmd
	$(HOST_BIN)/test_virgl_cmd

test_gfx2d: kernel/gfx/gfx2d.c kernel/gfx/gfx_fast.c tools/test_gfx2d.c kernel/gfx/gfx2d.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx -msse2 \
		kernel/gfx/gfx2d.c kernel/gfx/gfx_fast.c tools/test_gfx2d.c \
		-o $(HOST_BIN)/test_gfx2d

test_keystate: tools/test_keystate.c kernel/gfx/input.c kernel/gfx/input.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/gfx \
		tools/test_keystate.c kernel/gfx/input.c \
		-o $(HOST_BIN)/test_keystate
	$(HOST_BIN)/test_keystate

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

host-cfs-v5-test: tools/test_cfs_v5.c kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \
		-o $(HOST_BIN)/test_cfs_v5 tools/test_cfs_v5.c \
		kernel/fs/cfs.c kernel/fs/cfs_fsck.c
	$(HOST_BIN)/test_cfs_v5

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

disk-cube: $(DISK_IMG) host-cfs-put-file
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/CUBE.CC GAMES/CUBE.CC

disk-world: $(DISK_IMG) host-cfs-put-file
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/WORLD.CC GAMES/WORLD.CC

disk-watch: $(DISK_IMG) host-cfs-put-file
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/WATCH.CC GAMES/WATCH.CC

disk-blink: $(DISK_IMG) host-cfs-put-file
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/BLINK.CVA GAMES/BLINK.CVA

disk-doom: $(DISK_IMG) host-cfs-put-file GAMES/DOOM/DOOM1.WAD host-mk-clv
	$(HOST_BIN)/mk_clv GAMES/DOOM/ENGINE.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/ENGINE.CLV GAMES/DOOM/ENGINE.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/DOOM.CC GAMES/DOOM/DOOM.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/I_CHRIS.CC GAMES/DOOM/I_CHRIS.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/I_SOUND.CC GAMES/DOOM/I_SOUND.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/I_VIDEO.CC GAMES/DOOM/I_VIDEO.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/I_INPUT.CC GAMES/DOOM/I_INPUT.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/W_FILE.CC GAMES/DOOM/W_FILE.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/MAIN.CC GAMES/DOOM/MAIN.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/DOOM.LST GAMES/DOOM/DOOM.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/ENGINE.LST GAMES/DOOM/ENGINE.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/Makefile GAMES/DOOM/Makefile
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/DOOM1.WAD GAMES/DOOM/DOOM1.WAD
	@if [ -f GAMES/DOOM/DOOM1.MINI.WAD ]; then \
		$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/DOOM1.MINI.WAD GAMES/DOOM/DOOM1.MINI.WAD; \
	fi
	@while IFS= read -r f; do \
		f=$$(printf '%s' "$$f" | tr -d '\r'); \
		[ -n "$$f" ] || continue; \
		$(HOST_BIN)/cfs_put_file $(DISK_IMG) $$f $$f; \
	done < GAMES/DOOM/ENGINE.LST
	@for h in third_party/doomgeneric_src/doomgeneric/*.h; do \
		$(HOST_BIN)/cfs_put_file $(DISK_IMG) $$h $$h; \
	done

# Keep mini WAD recipe available but do not overwrite Freedoom IWAD.
GAMES/DOOM/DOOM1.MINI.WAD: tools/mk_miniwad.c
	mkdir -p GAMES/DOOM $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -o $(HOST_BIN)/mk_miniwad tools/mk_miniwad.c
	$(HOST_BIN)/mk_miniwad GAMES/DOOM/DOOM1.MINI.WAD

GAMES/DOOM/DOOM1.WAD:
	@test -s GAMES/DOOM/DOOM1.WAD || (echo "Missing GAMES/DOOM/DOOM1.WAD (shareware IWAD, <=8MiB CFS limit)" && exit 1)
test_doom_compile: tools/test_doom_compile.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_doom_compile.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-o $(HOST_BIN)/test_doom_compile
	$(HOST_BIN)/test_doom_compile

test_doom_engine: tools/test_doom_engine.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		tools/test_doom_engine.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		-o $(HOST_BIN)/test_doom_engine
	$(HOST_BIN)/test_doom_engine

doom-engine-clv: host-mk-clv
	$(HOST_BIN)/mk_clv GAMES/DOOM/ENGINE.LST

host-doom-jit-entry-test: doom-engine-clv tools/test_doom_jit_entry.c tools/jit_host_stub.c \
		compiler/jit/jit_emit.c compiler/jit/jit_compile.c \
		compiler/jit/jit_runtime.c compiler/clvm/clvm_format.c \
		compiler/clvm/clvm_vm.c kernel/gfx/gfx2d.c kernel/gfx/gfx_fast.c \
		kernel/gfx/zbuf.c GAMES/DOOM/ENGINE.CLV
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -DJIT_HOST_EXTERNAL_SERIAL \
		-Icompiler/jit -Icompiler/clvm -Icompiler -Ikernel/gfx -Ikernel/metal \
		-msse2 tools/jit_host_stub.c compiler/jit/jit_emit.c \
		compiler/jit/jit_compile.c compiler/jit/jit_runtime.c \
		compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		kernel/gfx/gfx2d.c kernel/gfx/gfx_fast.c kernel/gfx/zbuf.c \
		kernel/metal/spin.c \
		tools/test_doom_jit_entry.c -o $(HOST_BIN)/test_doom_jit_entry
	$(HOST_BIN)/test_doom_jit_entry

host-cfs-check-doom: tools/cfs_check_doom.c kernel/fs/cfs.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs -o $(HOST_BIN)/cfs_check_doom \
		tools/cfs_check_doom.c kernel/fs/cfs.c
	$(HOST_BIN)/cfs_check_doom $(DISK_IMG)

doom-qemu-jit-smoke: $(ISO) disk host-cfs-check-doom
	python3 tools/doom_qemu_jit_smoke.py

doom-qemu-long-smoke: $(ISO) disk host-cfs-check-doom
	python3 tools/doom_qemu_long_smoke.py

host-doom-gates: test_doom_compile test_doom_engine host-doom-jit-entry-test

host-stability-gates: host-editor-test host-graphics-present-test \
	host-task-window-test host-slot-front-test host-doom-gates test_editor_vi

disk-exit42: $(DISK_IMG) host-cfs-put-file
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SRC/EXIT42.S SRC/EXIT42.S
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SRC/EXIT42.C SRC/EXIT42.C

disk-lib: $(DISK_IMG) host-cfs-put-file
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STR.CC LIB/STR.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STR.H LIB/STR.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STDDEF.H LIB/STDDEF.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STDINT.H LIB/STDINT.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STDIO.H LIB/STDIO.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STDIO.CC LIB/STDIO.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STDARG.H LIB/STDARG.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STRING.H LIB/STRING.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STRING.CC LIB/STRING.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STRINGS.H LIB/STRINGS.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STDLIB.H LIB/STDLIB.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STDLIB.CC LIB/STDLIB.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/CTYPE.H LIB/CTYPE.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/CTYPE.CC LIB/CTYPE.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/MATH.H LIB/MATH.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/MATH.CC LIB/MATH.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/SETJMP.H LIB/SETJMP.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/THREADS.H LIB/THREADS.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/STDBOOL.H LIB/STDBOOL.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/INTTYPES.H LIB/INTTYPES.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/LIMITS.H LIB/LIMITS.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/ASSERT.H LIB/ASSERT.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/ERRNO.H LIB/ERRNO.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/UNISTD.H LIB/UNISTD.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/FCNTL.H LIB/FCNTL.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/TIME.H LIB/TIME.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/SYS_TYPES.H LIB/SYS_TYPES.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/SYS_STAT.H LIB/SYS_STAT.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/MATH3D.CC LIB/MATH3D.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/XFORM.H LIB/XFORM.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/BODY.CC LIB/BODY.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/CLIP.CC LIB/CLIP.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/ANIM.CC LIB/ANIM.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/SIM.CC LIB/SIM.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/HIT.CC LIB/HIT.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SRC/CAT.CC SRC/CAT.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SRC/HELLO.TXT SRC/HELLO.TXT
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/PHYS.CC GAMES/PHYS.CC

disk-ui: $(DISK_IMG) host-cfs-put-file host-mk-clv
	$(HOST_BIN)/mk_clv APPS/DESKTOP/DESKTOP.LST
	$(HOST_BIN)/mk_clv APPS/TASKBAR/TASKBAR.LST
	$(HOST_BIN)/mk_clv SYS/DRV/VIRTIOGPU.LST
	$(HOST_BIN)/mk_clv SYS/DRV/HWDISC.LST
	$(HOST_BIN)/mk_clv SYS/DRV/FORMAT.LST
	$(HOST_BIN)/mk_clv SYS/DRV/MKSTICK.LST
	$(HOST_BIN)/mk_clv SYS/DRV/INSTALL.LST
	$(HOST_BIN)/mk_clv SYS/ARCH/RISCV.LST
	$(HOST_BIN)/mk_clv APPS/SHELL/SHELL.LST
	$(HOST_BIN)/mk_clv APPS/EXPLORER/EXPLORER.LST
	$(HOST_BIN)/mk_clv APPS/EDITOR/EDITOR.LST
	$(HOST_BIN)/mk_clv APPS/TASKMGR/TASKMGR.LST
	$(HOST_BIN)/mk_clv APPS/BALL/BALL.LST
	$(HOST_BIN)/mk_clv APPS/PREFS/PREFS.LST
	$(HOST_BIN)/mk_clv GAMES/MINE/MINE.LST
	$(HOST_BIN)/mk_clv LIB/WIN.LST --cls LIB/WIN
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/WIN.H LIB/WIN.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/WIN.CC LIB/WIN.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/WIN.LST LIB/WIN.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/WIN.CLS LIB/WIN.CLS
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/UI.H LIB/UI.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/UI.CC LIB/UI.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/APP.H LIB/APP.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/APP.CC LIB/APP.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/CATALOG APPS/CATALOG
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/DESKTOP/DESKTOP.CC APPS/DESKTOP/DESKTOP.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/DESKTOP/DESKTOP.LST APPS/DESKTOP/DESKTOP.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/DESKTOP/DESKTOP.CLV APPS/DESKTOP/DESKTOP.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/VIRTIOGPU.CC SYS/DRV/VIRTIOGPU.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/VIRTIOGPU.LST SYS/DRV/VIRTIOGPU.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/VIRTIOGPU.CLV SYS/DRV/VIRTIOGPU.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/HWDISC.CC SYS/DRV/HWDISC.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/HWDISC.LST SYS/DRV/HWDISC.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/HWDISC.CLV SYS/DRV/HWDISC.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/FORMAT.CC SYS/DRV/FORMAT.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/FORMAT.LST SYS/DRV/FORMAT.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/FORMAT.CLV SYS/DRV/FORMAT.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/MKSTICK.CC SYS/DRV/MKSTICK.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/MKSTICK.LST SYS/DRV/MKSTICK.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/MKSTICK.CLV SYS/DRV/MKSTICK.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/INSTALL.CC SYS/DRV/INSTALL.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/INSTALL.LST SYS/DRV/INSTALL.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/INSTALL.CLV SYS/DRV/INSTALL.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/ARCH/RISCV.CC SYS/ARCH/RISCV.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/ARCH/RISCV.LST SYS/ARCH/RISCV.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/ARCH/RISCV.CLV SYS/ARCH/RISCV.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKBAR/TASKBAR.CC APPS/TASKBAR/TASKBAR.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKBAR/TASKBAR.LST APPS/TASKBAR/TASKBAR.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKBAR/TASKBAR.CLV APPS/TASKBAR/TASKBAR.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/SHELL/SHELL.CC APPS/SHELL/SHELL.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/SHELL/SHELL.LST APPS/SHELL/SHELL.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/SHELL/SHELL.CLV APPS/SHELL/SHELL.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EXPLORER/EXPLORER.CC APPS/EXPLORER/EXPLORER.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EXPLORER/EXPLORER.LST APPS/EXPLORER/EXPLORER.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EXPLORER/EXPLORER.CLV APPS/EXPLORER/EXPLORER.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EDITOR/EDITOR.CC APPS/EDITOR/EDITOR.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EDITOR/EDITOR.LST APPS/EDITOR/EDITOR.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EDITOR/EDITOR.CLV APPS/EDITOR/EDITOR.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKMGR/TASKMGR.CC APPS/TASKMGR/TASKMGR.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKMGR/TASKMGR.LST APPS/TASKMGR/TASKMGR.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKMGR/TASKMGR.CLV APPS/TASKMGR/TASKMGR.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/BALL/BALL.CC APPS/BALL/BALL.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/BALL/BALL.LST APPS/BALL/BALL.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/BALL/BALL.CLV APPS/BALL/BALL.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/PREFS/PREFS.CC APPS/PREFS/PREFS.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/PREFS/PREFS.LST APPS/PREFS/PREFS.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/PREFS/PREFS.CLV APPS/PREFS/PREFS.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/MINE/MINE.CC GAMES/MINE/MINE.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/MINE/MINE.LST GAMES/MINE/MINE.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/MINE/MINE.CLV GAMES/MINE/MINE.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/MINE/STATE.CC GAMES/MINE/STATE.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/MINE/CUBE.CC GAMES/MINE/CUBE.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/MINE/GEN.CC GAMES/MINE/GEN.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/MINE/AI.CC GAMES/MINE/AI.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/MINE/SKY.CC GAMES/MINE/SKY.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/MINE/SAVE.CC GAMES/MINE/SAVE.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/MINE/PLAY.CC GAMES/MINE/PLAY.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/MINE/UI.CC GAMES/MINE/UI.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/SIM.CC LIB/SIM.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/HIT.CC LIB/HIT.CC

disk-apps: disk-ui
	$(HOST_BIN)/mk_clv GAMES/DOOM/DOOM.LST
	$(HOST_BIN)/mk_clv GAMES/DOOM/ENGINE.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/DOOM.CLV GAMES/DOOM/DOOM.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/ENGINE.CLV GAMES/DOOM/ENGINE.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/DOOM.LST GAMES/DOOM/DOOM.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/ENGINE.LST GAMES/DOOM/ENGINE.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/DOOM.CC GAMES/DOOM/DOOM.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/MAIN.CC GAMES/DOOM/MAIN.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/I_CHRIS.CC GAMES/DOOM/I_CHRIS.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/I_INPUT.CC GAMES/DOOM/I_INPUT.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/I_VIDEO.CC GAMES/DOOM/I_VIDEO.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/I_SOUND.CC GAMES/DOOM/I_SOUND.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/W_FILE.CC GAMES/DOOM/W_FILE.CC
	@if [ -f GAMES/DOOM/DOOM1.WAD ]; then \
		$(HOST_BIN)/cfs_put_file $(DISK_IMG) GAMES/DOOM/DOOM1.WAD GAMES/DOOM/DOOM1.WAD; \
	fi
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/WIN.H LIB/WIN.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/WIN.CC LIB/WIN.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/WIN.LST LIB/WIN.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/WIN.CLS LIB/WIN.CLS
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/UI.H LIB/UI.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/UI.CC LIB/UI.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/APP.H LIB/APP.H
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) LIB/APP.CC LIB/APP.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/CATALOG APPS/CATALOG
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/DESKTOP/DESKTOP.CC APPS/DESKTOP/DESKTOP.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/DESKTOP/DESKTOP.LST APPS/DESKTOP/DESKTOP.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/DESKTOP/DESKTOP.CLV APPS/DESKTOP/DESKTOP.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/VIRTIOGPU.CC SYS/DRV/VIRTIOGPU.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/VIRTIOGPU.LST SYS/DRV/VIRTIOGPU.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/VIRTIOGPU.CLV SYS/DRV/VIRTIOGPU.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/HWDISC.CC SYS/DRV/HWDISC.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/HWDISC.LST SYS/DRV/HWDISC.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/HWDISC.CLV SYS/DRV/HWDISC.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/FORMAT.CC SYS/DRV/FORMAT.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/FORMAT.LST SYS/DRV/FORMAT.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/FORMAT.CLV SYS/DRV/FORMAT.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/MKSTICK.CC SYS/DRV/MKSTICK.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/MKSTICK.LST SYS/DRV/MKSTICK.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/MKSTICK.CLV SYS/DRV/MKSTICK.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/INSTALL.CC SYS/DRV/INSTALL.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/INSTALL.LST SYS/DRV/INSTALL.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/DRV/INSTALL.CLV SYS/DRV/INSTALL.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/ARCH/RISCV.CC SYS/ARCH/RISCV.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/ARCH/RISCV.LST SYS/ARCH/RISCV.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SYS/ARCH/RISCV.CLV SYS/ARCH/RISCV.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/DESKTOP/Makefile APPS/DESKTOP/Makefile
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKBAR/TASKBAR.CC APPS/TASKBAR/TASKBAR.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKBAR/TASKBAR.LST APPS/TASKBAR/TASKBAR.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKBAR/TASKBAR.CLV APPS/TASKBAR/TASKBAR.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKBAR/Makefile APPS/TASKBAR/Makefile
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/SHELL/SHELL.CC APPS/SHELL/SHELL.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/SHELL/SHELL.LST APPS/SHELL/SHELL.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/SHELL/SHELL.CLV APPS/SHELL/SHELL.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/SHELL/Makefile APPS/SHELL/Makefile
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EXPLORER/EXPLORER.CC APPS/EXPLORER/EXPLORER.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EXPLORER/EXPLORER.LST APPS/EXPLORER/EXPLORER.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EXPLORER/EXPLORER.CLV APPS/EXPLORER/EXPLORER.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EXPLORER/Makefile APPS/EXPLORER/Makefile
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EDITOR/EDITOR.CC APPS/EDITOR/EDITOR.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EDITOR/EDITOR.LST APPS/EDITOR/EDITOR.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EDITOR/EDITOR.CLV APPS/EDITOR/EDITOR.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/EDITOR/Makefile APPS/EDITOR/Makefile
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKMGR/TASKMGR.CC APPS/TASKMGR/TASKMGR.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKMGR/TASKMGR.LST APPS/TASKMGR/TASKMGR.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKMGR/TASKMGR.CLV APPS/TASKMGR/TASKMGR.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/TASKMGR/Makefile APPS/TASKMGR/Makefile
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/BALL/BALL.CC APPS/BALL/BALL.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/BALL/BALL.LST APPS/BALL/BALL.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/BALL/BALL.CLV APPS/BALL/BALL.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/BALL/Makefile APPS/BALL/Makefile
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/PREFS/PREFS.CC APPS/PREFS/PREFS.CC
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/PREFS/PREFS.LST APPS/PREFS/PREFS.LST
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/PREFS/PREFS.CLV APPS/PREFS/PREFS.CLV
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) APPS/PREFS/Makefile APPS/PREFS/Makefile
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) SRC/HELLO.TXT SRC/HELLO.TXT

host-mk-clv: tools/mk_clv.c compiler/chrisc/chrisc.c compiler/clvm/clasm.c \
		compiler/clvm/clvm_format.c compiler/cls/cls.c compiler/gc/gc.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
		-Icompiler/cls -Icompiler/gc -Icompiler \
		tools/mk_clv.c compiler/chrisc/chrisc.c compiler/clvm/clasm.c \
		compiler/clvm/clvm_format.c compiler/cls/cls.c compiler/gc/gc.c \
		-o $(HOST_BIN)/mk_clv

test_cls: tools/test_cls.c compiler/cls/cls.c compiler/gc/gc.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/cls -Icompiler/gc -Icompiler \
		tools/test_cls.c compiler/cls/cls.c compiler/gc/gc.c \
		-o $(HOST_BIN)/test_cls
	$(HOST_BIN)/test_cls

disk: disk-hello disk-fault disk-cube disk-world disk-watch disk-blink disk-exit42 disk-lib disk-doom disk-apps host-cfs-put
	$(HOST_BIN)/cfs_put $(DISK_IMG)

test_chrismake: tools/test_chrismake.c kernel/tools/chrismake.c kernel/tools/chrismake.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/tools \
		tools/test_chrismake.c kernel/tools/chrismake.c \
		-o $(HOST_BIN)/test_chrismake
	$(HOST_BIN)/test_chrismake

test_clasm: tools/test_clasm.c compiler/clvm/clasm.c compiler/clvm/clvm.h compiler/clvm/clasm.h
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/clvm \
		tools/test_clasm.c compiler/clvm/clasm.c \
		-o $(HOST_BIN)/test_clasm
	$(HOST_BIN)/test_clasm

test_clasm_games: tools/test_clasm_games.c compiler/clvm/clasm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/clvm \
		tools/test_clasm_games.c compiler/clvm/clasm.c \
		-o $(HOST_BIN)/test_clasm_games
	$(HOST_BIN)/test_clasm_games

test_native_link: tools/test_native_link.c compiler/chrisasm/chrisasm.c \
		compiler/chrisld/chriso.c compiler/chrisld/chrisld.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_CHRIS_INC) \
		tools/test_native_link.c compiler/chrisasm/chrisasm.c \
		compiler/chrisld/chriso.c compiler/chrisld/chrisld.c \
		-o $(HOST_BIN)/test_native_link
	$(HOST_BIN)/test_native_link

host-gfx3d: test_sse_init test_math3d test_math3d_view test_zbuf test_tri test_mesh test_cube_mesh test_cube_mesh_f test_chunk_mesh test_chrisc_arrays test_chrisc_float test_chrisc_ptr_float test_chrisc_move test_chrisc_fn test_chrisc_struct test_chrisc_trig test_chrisc_games test_chrisc_include test_chrisc_apps test_editor_vi test_chrisc_string test_chrisc_lang test_chrisc_c17 test_chrisc_doom test_doom_compile test_doom_engine test_cla_gc test_cls test_clasm test_clasm_games test_tile test_tile_bin

host-gates: host-cfs-test host-cfs-v5-test host-fsck-test host-cfs-paths-test \
	host-cfs-indirect-test host-cfs-journal-test host-cfs-chmod-test \
	host-cfs-maxwrite-test \
	host-jit-test host-jit-vm-test host-jit-native-test host-jit-bench-test host-chriso-test host-chrisasm-test host-chrisld-test \
	host-kcc-test test_native_link host-gfx3d test_chrismake host-stability-gates \
	host-input-test test_keystate test_gfx2d \
	host-virtq-test host-gpures-test host-virgl-cmd-test host-shader-test \
	host-pmm-heap-smp-test host-kthread-smp-test host-job-saturate-test \
	host-tlb-proto-test host-klog-test host-buildinfo-test \
	host-buildstamp-test host-meminfo-test host-pmm-cycle-test \
	host-clvm-sync-test host-elf-malformed-test host-sock-owner-test \
	host-cfs-lock-test host-gfx3d-ctx-test host-sys-write-test \
	host-fuzz-cfs-test host-fuzz-elf-test host-fuzz-chrisc-test \
	host-fuzz-clvm-test host-chrisc-read-diag-test test_chrisc_ptrwidth \
	test_editor_path test_cdbg test_dbg_step test_editmodel \
	host-gate-audit

host-gate-audit:
	python3 tools/check_test_gates.py

host-stress: host-pmm-heap-smp-test host-kthread-smp-test host-job-saturate-test \
	host-tlb-proto-test host-klog-test host-pmm-cycle-test \
	host-cfs-lock-test host-fuzz-cfs-test host-fuzz-elf-test \
	host-fuzz-chrisc-test host-fuzz-clvm-test host-task-window-test

qemu-stress: qemu-gates test-qemu-smp1

stability: host-gates host-sanitize
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
		$(MAKE) qemu-stress; \
	else \
		echo "stability: qemu-system-x86_64 absent, host gates only"; \
	fi

host-sanitize: tools/test_pmm_heap_smp.c tools/host_metal/metal_stub.c \
		kernel/metal/pmm.c kernel/metal/heap.c kernel/metal/spin.c \
		tools/test_kthread_smp.c kernel/metal/kthread.c kernel/metal/job.c \
		tools/test_job_saturate.c tools/test_keystate.c kernel/gfx/input.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -DCHRIS_HOST_METAL -pthread \
		-fsanitize=address,undefined -Itools/host_metal -Ikernel/metal \
		-o $(HOST_BIN)/test_pmm_heap_smp_asan tools/test_pmm_heap_smp.c \
		tools/host_metal/metal_stub.c kernel/metal/pmm.c kernel/metal/heap.c \
		kernel/metal/spin.c
	$(HOST_BIN)/test_pmm_heap_smp_asan
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -DCHRIS_HOST_METAL -pthread \
		-fsanitize=address,undefined -Ikernel/metal \
		-o $(HOST_BIN)/test_kthread_smp_asan tools/test_kthread_smp.c \
		tools/host_metal/metal_stub.c kernel/metal/kthread.c kernel/metal/job.c \
		kernel/metal/spin.c kernel/metal/tlb_proto.c
	$(HOST_BIN)/test_kthread_smp_asan
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
		-Ikernel/metal -o $(HOST_BIN)/test_job_saturate_asan \
		tools/test_job_saturate.c kernel/metal/job.c kernel/metal/spin.c \
		kernel/metal/tlb_proto.c
	$(HOST_BIN)/test_job_saturate_asan
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
		-Ikernel/gfx -o $(HOST_BIN)/test_keystate_asan \
		tools/test_keystate.c kernel/gfx/input.c
	$(HOST_BIN)/test_keystate_asan

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
		compiler/chrisld/chriso.c compiler/chrisld/chrisld.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_CHRIS_INC) \
		tools/test_kcc.c compiler/kcc/kcc.c compiler/chrisasm/chrisasm.c \
		compiler/chrisld/chriso.c compiler/chrisld/chrisld.c -o $(HOST_BIN)/test_kcc
	$(HOST_BIN)/test_kcc

host-kcc-kernel-l0: host-kcc-test

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

host-jit-vm-test: tools/test_jit_vm.c tools/jit_host_stub.c compiler/jit/jit_emit.c \
		compiler/jit/jit_compile.c compiler/jit/jit_runtime.c \
		compiler/chrisc/chrisc.c compiler/clvm/clasm.c compiler/clvm/clvm_format.c \
		compiler/clvm/clvm_vm.c kernel/gfx/gfx2d.c kernel/gfx/gfx_fast.c \
		kernel/gfx/zbuf.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/jit -Icompiler/chrisc \
		-Icompiler/clvm -Icompiler -Ikernel/lang -Ikernel/gfx -Ikernel/metal -msse2 \
		tools/jit_host_stub.c compiler/jit/jit_emit.c compiler/jit/jit_compile.c \
		compiler/jit/jit_runtime.c tools/test_jit_vm.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		kernel/gfx/gfx2d.c kernel/gfx/gfx_fast.c kernel/gfx/zbuf.c \
		kernel/metal/spin.c \
		-o $(HOST_BIN)/test_jit_vm
	$(HOST_BIN)/test_jit_vm

host-jit-native-test: tools/test_jit_native.c tools/jit_host_stub.c compiler/jit/jit_emit.c \
		compiler/jit/jit_compile.c compiler/jit/jit_runtime.c \
		compiler/chrisc/chrisc.c compiler/clvm/clasm.c compiler/clvm/clvm_format.c \
		compiler/clvm/clvm_vm.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/jit -Icompiler/chrisc \
		-Icompiler/clvm -Icompiler -Ikernel/lang -Ikernel/metal \
		tools/jit_host_stub.c compiler/jit/jit_emit.c compiler/jit/jit_compile.c \
		compiler/jit/jit_runtime.c tools/test_jit_native.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		kernel/metal/spin.c \
		-o $(HOST_BIN)/test_jit_native
	$(HOST_BIN)/test_jit_native

host-jit-bench-test: tools/test_jit_bench.c tools/jit_host_stub.c compiler/jit/jit_emit.c \
		compiler/jit/jit_compile.c compiler/jit/jit_runtime.c \
		compiler/chrisc/chrisc.c compiler/clvm/clasm.c compiler/clvm/clvm_format.c \
		compiler/clvm/clvm_vm.c kernel/gfx/gfx2d.c kernel/gfx/gfx_fast.c \
		kernel/gfx/zbuf.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L -Icompiler/jit -Icompiler/chrisc \
		-Icompiler/clvm -Icompiler -Ikernel/lang -Ikernel/gfx -Ikernel/metal -msse2 \
		tools/jit_host_stub.c compiler/jit/jit_emit.c compiler/jit/jit_compile.c \
		compiler/jit/jit_runtime.c tools/test_jit_bench.c compiler/chrisc/chrisc.c \
		compiler/clvm/clasm.c compiler/clvm/clvm_format.c compiler/clvm/clvm_vm.c \
		kernel/gfx/gfx2d.c kernel/gfx/gfx_fast.c kernel/gfx/zbuf.c \
		kernel/metal/spin.c \
		-o $(HOST_BIN)/test_jit_bench
	$(HOST_BIN)/test_jit_bench

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

kernel/gfx/font.c kernel/gfx/icons_tab.c build/icons/icons.S: tools/gen_ui_assets.py $(wildcard assets/*.png)
	python3 tools/gen_ui_assets.py

$(OBJ_DIR)/kernel/gfx/icons_bin.o: build/icons/icons.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c build/icons/icons.S -o $@

$(OBJ_DIR)/kernel/gfx/icons_tab.o: kernel/gfx/icons_tab.c
$(OBJ_DIR)/kernel/gfx/font.o: kernel/gfx/font.c

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

$(OBJ_DIR)/kernel/crypto/%.o: kernel/crypto/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/%.o: compiler/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/chrisc/%.o: compiler/chrisc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(GFX_FLOAT_CFLAGS) -O2 -c $< -o $@

$(OBJ_DIR)/compiler/clvm/%.o: compiler/clvm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/jit/%.o: compiler/jit/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/gc/%.o: compiler/gc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/il/%.o: compiler/il/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/cla/%.o: compiler/cla/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/compiler/cls/%.o: compiler/cls/%.c
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

$(HOST_BIN)/stamp_kernel: tools/stamp_kernel.c tools/buildstamp.c kernel/crypto/sha256.c
	mkdir -p $(HOST_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Ikernel/crypto \
		-o $@ tools/stamp_kernel.c tools/buildstamp.c kernel/crypto/sha256.c

$(KERNEL): $(OBJECTS) kernel/metal/linker.ld $(HOST_BIN)/stamp_kernel
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	$(HOST_BIN)/stamp_kernel $@

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

run-virgl: $(ISO) disk-ui run-stop
	@test -f $(DISK_IMG) || $(MAKE) disk.img
	$(QEMU) -M pc -m $(QEMU_MEM) -smp 4 -boot order=dc \
		-display gtk,gl=on,zoom-to-fit=on \
		-device virtio-vga-gl \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0 \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-serial stdio -no-reboot -no-shutdown \
		-cpu qemu64 -accel kvm

run: $(ISO) disk-ui run-stop
	@test -f $(DISK_IMG) || $(MAKE) disk.img
	@test -f $(BUILD_DIR)/ahci.img || dd if=/dev/zero of=$(BUILD_DIR)/ahci.img bs=1M count=32 status=none
	@test -f $(BUILD_DIR)/nvme.img || dd if=/dev/zero of=$(BUILD_DIR)/nvme.img bs=1M count=32 status=none
	@test -f $(BUILD_DIR)/vblk.img || dd if=/dev/zero of=$(BUILD_DIR)/vblk.img bs=1M count=32 status=none
	@test -f $(BUILD_DIR)/usb.img || dd if=/dev/zero of=$(BUILD_DIR)/usb.img bs=1M count=32 status=none
	@sleep 1
	$(QEMU) -M pc -m $(QEMU_MEM) -smp 4 -boot order=dc \
		-display $(QEMU_DISPLAY) \
		-usb -device usb-tablet,bus=usb-bus.0,port=2 \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0 \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-device virtio-gpu-pci \
		-drive if=none,id=ahcidisk,file=$(BUILD_DIR)/ahci.img,format=raw \
		-device ich9-ahci,id=ahci \
		-device ide-hd,drive=ahcidisk,bus=ahci.0 \
		-drive if=none,id=nvmedisk,file=$(BUILD_DIR)/nvme.img,format=raw \
		-device nvme,serial=chris,drive=nvmedisk \
		-drive if=none,id=vblk,file=$(BUILD_DIR)/vblk.img,format=raw \
		-device virtio-blk-pci,drive=vblk \
		-drive if=none,id=usbdisk,file=$(BUILD_DIR)/usb.img,format=raw \
		-device usb-storage,bus=usb-bus.0,port=1,drive=usbdisk \
		-device virtio-net-pci,netdev=n0 \
		-device AC97 \
		-netdev user,id=n0,hostfwd=udp:127.0.0.1:$(HOST_NET_PORT)-:7,hostfwd=tcp:127.0.0.1:$(HOST_NET_PORT)-:7,hostfwd=tcp:127.0.0.1:$(HOST_XFER_PORT)-:9016 \
		-serial stdio -no-reboot -no-shutdown \
		-cpu qemu64 -accel kvm

RISCV_CC ?= riscv64-unknown-elf-gcc
RISCV_ELF := $(BUILD_DIR)/riscv/kernel.elf

.PHONY: riscv
riscv: $(RISCV_ELF)

$(RISCV_ELF): kernel/arch/riscv/boot.S kernel/arch/riscv/main.c kernel/arch/riscv/link.ld \
	compiler/clvm/clvm_vm.c compiler/clvm/clvm_format.c
	@mkdir -p $(BUILD_DIR)/riscv
	clang --target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 \
		-std=c11 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector \
		-fno-pic -mcmodel=medany -O2 -DCHRIS_RISCV -Icompiler/clvm -Icompiler \
		-nostdlib -fuse-ld=lld -Wl,-T,kernel/arch/riscv/link.ld -o $@ \
		kernel/arch/riscv/boot.S kernel/arch/riscv/main.c \
		compiler/clvm/clvm_vm.c compiler/clvm/clvm_format.c

chrisvm:
	$(MAKE) -C chrisvm

chrisvm-test:
	$(MAKE) -C chrisvm test

clean:
	rm -rf $(BUILD_DIR)
	rm -f kernel/metal/*.o kernel/gfx/*.o kernel/wm/*.o kernel/tools/*.o \
		kernel/fs/*.o kernel/lang/*.o kernel/net/*.o compiler/*.o \
		compiler/chrisc/*.o compiler/clvm/*.o compiler/jit/*.o \
		compiler/chrisld/*.o compiler/chrisasm/*.o compiler/kcc/*.o \
		os.iso disk.img disk_v2.img

include scripts/qemu.mk
