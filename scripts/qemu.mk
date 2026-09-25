.PHONY: run-riscv qemu-gates full-gates \
	test-qemu-ata test-qemu-ahci test-qemu-nvme test-qemu-vblk \
	test-qemu-usb test-qemu-gpu test-qemu-install test-qemu-riscv \
	test-qemu-noata test-qemu-smp1 test-qemu-safe test-qemu-xhci \
	test-qemu-virgl

run-riscv: $(RISCV_ELF)
	@test -f $(BUILD_DIR)/vblk.img || dd if=/dev/zero of=$(BUILD_DIR)/vblk.img bs=1M count=32 status=none
	qemu-system-riscv64 -M virt -bios default -kernel $(RISCV_ELF) -display none \
		-serial stdio \
		-drive if=none,id=vd,file=$(BUILD_DIR)/vblk.img,format=raw,file.locking=off \
		-device virtio-blk-device,drive=vd -device virtio-gpu-device

QEMU_SMP ?= 4
QEMU_HEAD = -M pc -m 2048 -smp $(QEMU_SMP) -display none \
	-serial file:$(BUILD_DIR)/qemu-test.txt -no-reboot -cpu qemu64 -accel tcg
QEMU_GATE = python3 tools/qemu_gate.py --log $(BUILD_DIR)/qemu-test.txt

test-qemu-ata: $(ISO) $(DISK_IMG)
	rm -f $(BUILD_DIR)/qemu-test.txt
	$(QEMU_GATE) --timeout 40 \
		--expect "cpu_online_count=$(QEMU_SMP)" \
		--expect "root ata" --expect "cfs mounted" -- \
		$(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom

test-qemu-ahci: $(ISO) $(DISK_IMG)
	dd if=/dev/zero of=$(BUILD_DIR)/ahci.img bs=1M count=32 status=none
	rm -f $(BUILD_DIR)/qemu-test.txt
	$(QEMU_GATE) --timeout 40 \
		--expect "ahci disk sectors=" --expect "bdev rw ok ahci" -- \
		$(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-drive if=none,id=ahcidisk,file=$(BUILD_DIR)/ahci.img,format=raw,file.locking=off \
		-device ich9-ahci,id=ahci -device ide-hd,drive=ahcidisk,bus=ahci.0

test-qemu-nvme: $(ISO) $(DISK_IMG)
	dd if=/dev/zero of=$(BUILD_DIR)/nvme.img bs=1M count=32 status=none
	rm -f $(BUILD_DIR)/qemu-test.txt
	$(QEMU_GATE) --timeout 40 \
		--expect "nvme disk sectors=" --expect "bdev rw ok nvme" -- \
		$(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-drive if=none,id=nvmedisk,file=$(BUILD_DIR)/nvme.img,format=raw,file.locking=off \
		-device nvme,serial=chris,drive=nvmedisk

test-qemu-vblk: $(ISO) $(DISK_IMG)
	dd if=/dev/zero of=$(BUILD_DIR)/vblk.img bs=1M count=32 status=none
	rm -f $(BUILD_DIR)/qemu-test.txt
	$(QEMU_GATE) --timeout 40 \
		--expect "virtio-blk sectors=" --expect "bdev rw ok virtio-blk" -- \
		$(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-drive if=none,id=vblk,file=$(BUILD_DIR)/vblk.img,format=raw,file.locking=off \
		-device virtio-blk-pci,drive=vblk

test-qemu-usb: $(ISO) $(DISK_IMG)
	dd if=/dev/zero of=$(BUILD_DIR)/usb.img bs=1M count=32 status=none
	rm -f $(BUILD_DIR)/qemu-test.txt
	$(QEMU_GATE) --timeout 50 \
		--expect "usb msc sectors=" --expect "bdev rw ok usb" -- \
		$(QEMU) $(QEMU_HEAD) -boot order=dc \
		-usb -device usb-tablet,bus=usb-bus.0,port=2 \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-drive if=none,id=usbdisk,file=$(BUILD_DIR)/usb.img,format=raw,file.locking=off \
		-device usb-storage,bus=usb-bus.0,port=1,drive=usbdisk

$(BUILD_DIR)/os-virgl.iso: $(ISO)
	python3 -c "import pathlib; p=pathlib.Path('$(ISO_ROOT)/boot/limine/limine.conf'); t=p.read_text(); needle='resolution: 1920x1080x32\n'; ins=needle+'    cmdline: gfx.3d=virgl gfx.stress gfx.virgl.debug\n'; p.write_text(t.replace(needle, ins, 1) if 'cmdline:' not in t else t)"
	$(XORRISO) -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image \
		--protective-msdos-label $(ISO_ROOT) -o $@; \
	st=$$?; cp $(LIMINE_CONF_SRC) $(ISO_ROOT)/boot/limine/limine.conf; exit $$st
	$(LIMINE_DIR)/limine bios-install $@

test-qemu-virgl: $(BUILD_DIR)/os-virgl.iso $(DISK_IMG)
	@if ! $(QEMU) -device help 2>/dev/null | grep -q 'virtio-vga-gl\|virtio-gpu-gl'; then \
		echo "SKIP: host QEMU lacks VirGL support"; exit 2; \
	fi
	@disp=""; \
	if [ -e /dev/dri/renderD128 ] && $(QEMU) -display help 2>/dev/null | grep -q egl-headless; then \
		disp="egl-headless"; \
	elif [ -n "$$DISPLAY" ] && $(QEMU) -display help 2>/dev/null | grep -q gtk && ldconfig -p 2>/dev/null | grep -q libEGL; then \
		disp="gtk,gl=on"; \
	fi; \
	if [ -z "$$disp" ]; then \
		echo "SKIP: host QEMU lacks VirGL support"; exit 2; \
	fi; \
	echo "virgl display $$disp"; \
	rm -f $(BUILD_DIR)/qemu-virgl.txt; \
	$(QEMU_GATE) --timeout 180 --log $(BUILD_DIR)/qemu-virgl.txt \
		--expect "VIRGL feature: yes" \
		--expect "PASS: virgl clear" \
		--expect "PASS: virgl triangle" \
		--expect "PASS: virgl cube" \
		--expect "PASS: virgl depth" \
		--expect "PASS: virgl textured cube" \
		--expect "PASS: virgl present" \
		--expect "3D backend -> virgl" -- \
		$(QEMU) -M pc -m 2048 -smp 1 -boot order=dc -display $$disp \
		-serial file:$(BUILD_DIR)/qemu-virgl.txt -no-reboot -cpu qemu64 -accel tcg \
		-device virtio-vga-gl \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(BUILD_DIR)/os-virgl.iso,format=raw,if=ide,index=2,media=cdrom

test-qemu-gpu: $(ISO) $(DISK_IMG)
	rm -f $(BUILD_DIR)/qemu-test.txt
	$(QEMU_GATE) --timeout 40 --expect "virtio-gpu ready" -- \
		$(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-device virtio-gpu-pci

test-qemu-riscv: $(RISCV_ELF)
	dd if=/dev/zero of=$(BUILD_DIR)/vblk.img bs=1M count=32 status=none
	rm -f $(BUILD_DIR)/riscv-serial.txt
	python3 tools/qemu_gate.py --timeout 12 --log $(BUILD_DIR)/riscv-serial.txt \
		--expect "riscv kernel" --expect "sv39 on" --expect "clvm halt" \
		--expect "virtio-blk " --expect "virtio-gpu detected" -- \
		qemu-system-riscv64 -M virt -bios default -kernel $(RISCV_ELF) \
		-display none -serial file:$(BUILD_DIR)/riscv-serial.txt \
		-drive if=none,id=vd,file=$(BUILD_DIR)/vblk.img,format=raw,file.locking=off \
		-device virtio-blk-device,drive=vd -device virtio-gpu-device

disk-boot-payload: $(DISK_IMG) $(KERNEL) host-cfs-put-file \
		$(LIMINE_DIR)/BOOTX64.EFI $(LIMINE_CONF_SRC)
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) BOOT/KERNEL.ELF $(KERNEL)
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) EFI/BOOT/BOOTX64.EFI $(LIMINE_DIR)/BOOTX64.EFI
	$(HOST_BIN)/cfs_put_file $(DISK_IMG) BOOT/LIMINE.CFG $(LIMINE_CONF_SRC)

test-qemu-noata: $(ISO) $(DISK_IMG) disk-boot-payload
	rm -f $(BUILD_DIR)/qemu-test.txt
	$(QEMU_GATE) --timeout 90 \
		--expect "ata missing" --expect "root ahci" --expect "cfs mounted" \
		--expect "install selftest ok" --expect "desktop 60Hz" -- \
		$(QEMU) $(QEMU_HEAD) -boot order=d \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom,file.locking=off \
		-drive if=none,id=ahcidisk,file=$(DISK_IMG),format=raw,file.locking=off \
		-device ich9-ahci,id=ahci -device ide-hd,drive=ahcidisk,bus=ahci.0

test-qemu-install: $(ISO) $(DISK_IMG) host-cfs-put-file
	rm -f $(BUILD_DIR)/install-src.img $(BUILD_DIR)/install-target.img $(BUILD_DIR)/install-auto.txt
	cp $(DISK_IMG) $(BUILD_DIR)/install-src.img
	$(HOST_BIN)/cfs_put_file $(BUILD_DIR)/install-src.img BOOT/KERNEL.ELF $(KERNEL)
	$(HOST_BIN)/cfs_put_file $(BUILD_DIR)/install-src.img EFI/BOOT/BOOTX64.EFI $(LIMINE_DIR)/BOOTX64.EFI
	$(HOST_BIN)/cfs_put_file $(BUILD_DIR)/install-src.img BOOT/LIMINE.CFG iso_root/boot/limine/limine.conf
	printf 'install\n' > $(BUILD_DIR)/install-auto.txt
	printf 'ahci\n' > $(BUILD_DIR)/install-target-name.txt
	$(HOST_BIN)/cfs_put_file $(BUILD_DIR)/install-src.img BOOT/INSTALL.AUTO $(BUILD_DIR)/install-auto.txt
	$(HOST_BIN)/cfs_put_file $(BUILD_DIR)/install-src.img BOOT/INSTALL.TARGET $(BUILD_DIR)/install-target-name.txt
	dd if=/dev/zero of=$(BUILD_DIR)/install-target.img bs=1M count=560 status=none
	rm -f $(BUILD_DIR)/qemu-test.txt
	$(QEMU_GATE) --timeout 300 \
		--expect "install auto" --expect "install tree copied" \
		--expect "install gpt+esp+cfs disk=ahci" -- \
		$(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(BUILD_DIR)/install-src.img,format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom,file.locking=off \
		-drive if=none,id=target,file=$(BUILD_DIR)/install-target.img,format=raw,file.locking=off \
		-device ich9-ahci,id=ahci -device ide-hd,drive=target,bus=ahci.0
	python3 tools/check_install_img.py $(BUILD_DIR)/install-target.img
	cp /usr/share/OVMF/OVMF_VARS_4M.fd $(BUILD_DIR)/ovmf-vars.fd
	rm -f $(BUILD_DIR)/qemu-test.txt
	$(QEMU_GATE) --timeout 300 \
		--expect "cfs mounted" --expect "desktop 60Hz" -- \
		$(QEMU) -M pc -m 2048 -smp $(QEMU_SMP) -display none \
		-serial file:$(BUILD_DIR)/qemu-test.txt \
		-no-reboot -cpu qemu64 -accel tcg \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-drive if=pflash,format=raw,file=$(BUILD_DIR)/ovmf-vars.fd \
		-drive file=$(BUILD_DIR)/install-target.img,format=raw,if=ide,index=0,file.locking=off

test-qemu-smp1: $(ISO) $(DISK_IMG)
	$(MAKE) test-qemu-ata QEMU_SMP=1

$(BUILD_DIR)/os-safe.iso: $(ISO)
	python3 -c "import pathlib; p=pathlib.Path('$(ISO_ROOT)/boot/limine/limine.conf'); t=p.read_text(); needle='resolution: 1920x1080x32\n'; ins=needle+'    cmdline: safe\n'; p.write_text(t.replace(needle, ins, 1) if 'cmdline:' not in t else t)"
	$(XORRISO) -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image \
		--protective-msdos-label $(ISO_ROOT) -o $@; \
	st=$$?; cp $(LIMINE_CONF_SRC) $(ISO_ROOT)/boot/limine/limine.conf; exit $$st
	$(LIMINE_DIR)/limine bios-install $@

test-qemu-xhci: $(ISO) $(DISK_IMG)
	rm -f $(BUILD_DIR)/qemu-test.txt
	$(QEMU_GATE) --timeout 90 \
		--expect "xhci hid ready" -- \
		$(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-device qemu-xhci,id=xhci \
		-device usb-kbd,bus=xhci.0 \
		-device usb-mouse,bus=xhci.0

test-qemu-safe: $(BUILD_DIR)/os-safe.iso $(DISK_IMG)
	rm -f $(BUILD_DIR)/qemu-test.txt
	$(QEMU_GATE) --timeout 90 \
		--expect "safe mode" --expect "smp off" --expect "desktop 60Hz" -- \
		$(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(BUILD_DIR)/os-safe.iso,format=raw,if=ide,index=2,media=cdrom

qemu-gates: test-qemu-ata test-qemu-ahci test-qemu-nvme test-qemu-vblk test-qemu-usb test-qemu-gpu test-qemu-riscv test-qemu-noata test-qemu-install test-qemu-safe test-qemu-xhci

full-gates: host-gates qemu-gates test-qemu-smp1
