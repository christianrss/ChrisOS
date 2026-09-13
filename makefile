all: bootloader

bootloader:
	nasm boot/boot.asm -f bin -o boot/bin/boot.bin
	nasm boot/kernel_entry.asm -f elf -o boot/bin/kernel_entry.bin
	
	gcc -m32 -ffreestanding -fno-pie -c boot/final.c -o boot/bin/kernel.o
	ld -m elf_i386 -T boot/linker.ld -o boot/bin/kernel.elf boot/bin/kernel_entry.bin boot/bin/kernel.o

	objcopy -O binary boot/bin/kernel.elf boot/bin/kernel.bin
	cat boot/bin/boot.bin boot/bin/kernel.bin > os.img

clear:
	rm -f boot/boot.img

run:
	qemu-system-x86_64 -L "/usr/share/qemu" -drive format=raw,file=os.img
