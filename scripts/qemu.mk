.PHONY: run-riscv qemu-gates full-gates \
	test-qemu-ata test-qemu-ahci test-qemu-nvme test-qemu-vblk \
	test-qemu-usb test-qemu-gpu test-qemu-install test-qemu-riscv \
	test-qemu-noata

run-riscv: $(RISCV_ELF)
	@test -f $(BUILD_DIR)/vblk.img || dd if=/dev/zero of=$(BUILD_DIR)/vblk.img bs=1M count=32 status=none
	qemu-system-riscv64 -M virt -bios default -kernel $(RISCV_ELF) -display none \
		-serial stdio \
		-drive if=none,id=vd,file=$(BUILD_DIR)/vblk.img,format=raw,file.locking=off \
		-device virtio-blk-device,drive=vd -device virtio-gpu-device

QEMU_HEAD = -M pc -m 2048 -display none -serial file:$(BUILD_DIR)/qemu-test.txt -no-reboot -cpu qemu64 -accel tcg

test-qemu-ata: $(ISO) $(DISK_IMG)
	rm -f $(BUILD_DIR)/qemu-test.txt
	-timeout 40 $(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom
	grep -a "root ata" $(BUILD_DIR)/qemu-test.txt
	grep -a "cfs mounted" $(BUILD_DIR)/qemu-test.txt

test-qemu-ahci: $(ISO) $(DISK_IMG)
	@test -f $(BUILD_DIR)/ahci.img || dd if=/dev/zero of=$(BUILD_DIR)/ahci.img bs=1M count=32 status=none
	rm -f $(BUILD_DIR)/qemu-test.txt
	-timeout 40 $(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-drive if=none,id=ahcidisk,file=$(BUILD_DIR)/ahci.img,format=raw,file.locking=off \
		-device ich9-ahci,id=ahci -device ide-hd,drive=ahcidisk,bus=ahci.0
	grep -a "ahci disk sectors=" $(BUILD_DIR)/qemu-test.txt
	grep -a "bdev rw ok ahci" $(BUILD_DIR)/qemu-test.txt

test-qemu-nvme: $(ISO) $(DISK_IMG)
	@test -f $(BUILD_DIR)/nvme.img || dd if=/dev/zero of=$(BUILD_DIR)/nvme.img bs=1M count=32 status=none
	rm -f $(BUILD_DIR)/qemu-test.txt
	-timeout 40 $(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-drive if=none,id=nvmedisk,file=$(BUILD_DIR)/nvme.img,format=raw,file.locking=off \
		-device nvme,serial=chris,drive=nvmedisk
	grep -a "nvme disk sectors=" $(BUILD_DIR)/qemu-test.txt
	grep -a "bdev rw ok nvme" $(BUILD_DIR)/qemu-test.txt

test-qemu-vblk: $(ISO) $(DISK_IMG)
	@test -f $(BUILD_DIR)/vblk.img || dd if=/dev/zero of=$(BUILD_DIR)/vblk.img bs=1M count=32 status=none
	rm -f $(BUILD_DIR)/qemu-test.txt
	-timeout 40 $(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-drive if=none,id=vblk,file=$(BUILD_DIR)/vblk.img,format=raw,file.locking=off \
		-device virtio-blk-pci,drive=vblk
	grep -a "virtio-blk sectors=" $(BUILD_DIR)/qemu-test.txt
	grep -a "bdev rw ok virtio-blk" $(BUILD_DIR)/qemu-test.txt

test-qemu-usb: $(ISO) $(DISK_IMG)
	@test -f $(BUILD_DIR)/usb.img || dd if=/dev/zero of=$(BUILD_DIR)/usb.img bs=1M count=32 status=none
	rm -f $(BUILD_DIR)/qemu-test.txt
	-timeout 50 $(QEMU) $(QEMU_HEAD) -boot order=dc \
		-usb -device usb-tablet,bus=usb-bus.0,port=2 \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-drive if=none,id=usbdisk,file=$(BUILD_DIR)/usb.img,format=raw,file.locking=off \
		-device usb-storage,bus=usb-bus.0,port=1,drive=usbdisk
	grep -a "usb msc sectors=" $(BUILD_DIR)/qemu-test.txt
	grep -a "bdev rw ok usb" $(BUILD_DIR)/qemu-test.txt

test-qemu-gpu: $(ISO) $(DISK_IMG)
	rm -f $(BUILD_DIR)/qemu-test.txt
	-timeout 40 $(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom \
		-device virtio-gpu-pci
	grep -a "virtio-gpu ready" $(BUILD_DIR)/qemu-test.txt

test-qemu-riscv: $(RISCV_ELF)
	@test -f $(BUILD_DIR)/vblk.img || dd if=/dev/zero of=$(BUILD_DIR)/vblk.img bs=1M count=32 status=none
	rm -f $(BUILD_DIR)/riscv-serial.txt
	-timeout 12 qemu-system-riscv64 -M virt -bios default -kernel $(RISCV_ELF) \
		-display none -serial file:$(BUILD_DIR)/riscv-serial.txt \
		-drive if=none,id=vd,file=$(BUILD_DIR)/vblk.img,format=raw,file.locking=off \
		-device virtio-blk-device,drive=vd -device virtio-gpu-device
	grep -a "riscv kernel" $(BUILD_DIR)/riscv-serial.txt
	grep -a "sv39 on" $(BUILD_DIR)/riscv-serial.txt
	grep -a "irq " $(BUILD_DIR)/riscv-serial.txt
	grep -a "clvm halt" $(BUILD_DIR)/riscv-serial.txt
	grep -a "virtio-blk " $(BUILD_DIR)/riscv-serial.txt
	grep -a "virtio-gpu detected" $(BUILD_DIR)/riscv-serial.txt
	grep -a "virtio-gpu cmd submitted" $(BUILD_DIR)/riscv-serial.txt

test-qemu-noata: $(ISO) $(DISK_IMG)
	rm -f $(BUILD_DIR)/qemu-test.txt
	-timeout 90 $(QEMU) $(QEMU_HEAD) -boot order=d \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom,file.locking=off \
		-drive if=none,id=ahcidisk,file=$(DISK_IMG),format=raw,file.locking=off \
		-device ich9-ahci,id=ahci -device ide-hd,drive=ahcidisk,bus=ahci.0
	grep -a "ata missing" $(BUILD_DIR)/qemu-test.txt
	grep -a "root ahci" $(BUILD_DIR)/qemu-test.txt
	grep -a "cfs mounted" $(BUILD_DIR)/qemu-test.txt
	grep -a "install selftest ok" $(BUILD_DIR)/qemu-test.txt
	grep -a "desktop 60Hz" $(BUILD_DIR)/qemu-test.txt

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
	-timeout 300 $(QEMU) $(QEMU_HEAD) -boot order=dc \
		-drive file=$(BUILD_DIR)/install-src.img,format=raw,if=ide,index=0,file.locking=off \
		-drive file=$(ISO),format=raw,if=ide,index=2,media=cdrom,file.locking=off \
		-drive if=none,id=target,file=$(BUILD_DIR)/install-target.img,format=raw,file.locking=off \
		-device ich9-ahci,id=ahci -device ide-hd,drive=target,bus=ahci.0
	grep -a "install auto" $(BUILD_DIR)/qemu-test.txt
	grep -a "install tree copied" $(BUILD_DIR)/qemu-test.txt
	grep -a "install gpt+esp+cfs disk=ahci" $(BUILD_DIR)/qemu-test.txt
	python3 tools/check_install_img.py $(BUILD_DIR)/install-target.img
	cp /usr/share/OVMF/OVMF_VARS_4M.fd $(BUILD_DIR)/ovmf-vars.fd
	rm -f $(BUILD_DIR)/qemu-test.txt
	-timeout 300 $(QEMU) -M pc -m 2048 -display none -serial file:$(BUILD_DIR)/qemu-test.txt \
		-no-reboot -cpu qemu64 -accel tcg \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-drive if=pflash,format=raw,file=$(BUILD_DIR)/ovmf-vars.fd \
		-drive file=$(BUILD_DIR)/install-target.img,format=raw,if=ide,index=0,file.locking=off
	grep -a "cfs mounted" $(BUILD_DIR)/qemu-test.txt
	grep -a "desktop 60Hz" $(BUILD_DIR)/qemu-test.txt

qemu-gates: test-qemu-ata test-qemu-ahci test-qemu-nvme test-qemu-vblk test-qemu-usb test-qemu-gpu test-qemu-riscv test-qemu-noata

full-gates: host-gates qemu-gates
