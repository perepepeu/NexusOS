include toolchain.mk

.DEFAULT_GOAL := iso
QEMU := qemu-system-x86_64
QEMUFLAGS := -machine q35 -accel tcg -m 256M -boot d -net none -no-reboot
OVMF ?= /usr/share/OVMF/OVMF_CODE_4M.fd
DISPLAY_BACKEND ?= gtk
KERNEL_OBJECTS := build/kernel/entry.o build/kernel/main.o build/kernel/console.o build/kernel/serial.o build/kernel/log.o build/kernel/panic.o build/kernel/interrupts.o build/kernel/interrupt_stubs.o build/kernel/keyboard.o

.PHONY: check-toolchain
check-toolchain:
	bash scripts/check-toolchain.sh "$(CC)" "$(AS)" "$(LD)" "$(ARCH)" "$(CFLAGS)" "$(ASFLAGS)" "$(LDFLAGS)"

.PHONY: setup-limine kernel iso run run-uefi check-kernel check-kernel-arch
setup-limine:
	bash scripts/setup-limine.sh

KERNEL_CFLAGS := $(CFLAGS) -mcmodel=kernel -fno-asynchronous-unwind-tables -fno-unwind-tables -O2 -Ikernel/include -MMD -MP

kernel: build/kernel.elf

check-kernel-arch:
	@test "$(ARCH)" = x86_64 || { echo "O kernel inicial suporta apenas ARCH=x86_64."; exit 1; }

build/kernel/%.o: kernel/%.c toolchain.mk Makefile | check-kernel-arch
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/kernel/entry.o: kernel/entry.asm toolchain.mk Makefile | check-kernel-arch
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

build/kernel/interrupt_stubs.o: kernel/interrupts.asm toolchain.mk Makefile | check-kernel-arch
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

build/kernel.elf: $(KERNEL_OBJECTS) kernel/linker.ld | check-kernel-arch
	$(LD) $(LDFLAGS) -z max-page-size=0x1000 -T kernel/linker.ld $(KERNEL_OBJECTS) -o $@

iso: kernel setup-limine
	bash scripts/build-iso.sh

run: iso
	$(QEMU) $(QEMUFLAGS) -cdrom build/nexusos.iso -display $(DISPLAY_BACKEND) -serial mon:stdio

run-uefi: iso
	$(QEMU) $(QEMUFLAGS) -cdrom build/nexusos.iso -display $(DISPLAY_BACKEND) -serial mon:stdio -drive if=pflash,format=raw,readonly=on,file=$(OVMF)

check-kernel: iso
	python3 scripts/check-kernel.py

.PHONY: check-errors
check-errors: iso
	python3 scripts/check-kernel.py --errors

.PHONY: check-interrupts
check-interrupts: iso
	python3 scripts/check-kernel.py --interrupts

.PHONY: check-keyboard
check-keyboard: iso
	python3 scripts/check-kernel.py --keyboard

# Compatibilidade com os comandos da etapa anterior: agora incluem o kernel.
.PHONY: bootloader-iso run-bootloader run-bootloader-uefi check-bootloader
bootloader-iso: iso
run-bootloader: run
run-bootloader-uefi: run-uefi
check-bootloader: check-kernel

-include $(wildcard build/kernel/*.d)
