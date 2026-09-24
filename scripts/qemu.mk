.PHONY: run-riscv qemu-gates full-gates \
	test-qemu-ata test-qemu-ahci test-qemu-nvme test-qemu-vblk \
	test-qemu-usb test-qemu-gpu test-qemu-install test-qemu-riscv \
	test-qemu-noata test-qemu-smp1

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

test-qemu-noata: $(ISO) $(DISK_IMG)
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
	$(HOST_BIN)/cfs_put_file $(BUILD_DIR)/install-src.img BOOT/INSTALL.AUTO $(BUILD_DIR)/install-auto.txt
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

qemu-gates: test-qemu-ata test-qemu-ahci test-qemu-nvme test-qemu-vblk test-qemu-usb test-qemu-gpu test-qemu-riscv test-qemu-noata test-qemu-install

full-gates: host-gates qemu-gates test-qemu-smp1
