include toolchain.mk

.DEFAULT_GOAL := check-toolchain
QEMU := qemu-system-x86_64
QEMUFLAGS := -machine q35 -accel tcg -m 256M -boot d -net none -no-reboot
OVMF ?= /usr/share/OVMF/OVMF_CODE_4M.fd

.PHONY: check-toolchain
check-toolchain:
	bash scripts/check-toolchain.sh "$(CC)" "$(AS)" "$(LD)" "$(ARCH)" "$(CFLAGS)" "$(ASFLAGS)" "$(LDFLAGS)"

.PHONY: setup-limine bootloader-iso run-bootloader run-bootloader-uefi check-bootloader
setup-limine:
	bash scripts/setup-limine.sh

bootloader-iso: setup-limine
	bash scripts/build-bootloader-iso.sh

run-bootloader: bootloader-iso
	$(QEMU) $(QEMUFLAGS) -cdrom build/nexusos-bootloader.iso -display none -serial mon:stdio

run-bootloader-uefi: bootloader-iso
	$(QEMU) $(QEMUFLAGS) -cdrom build/nexusos-bootloader.iso -display none -serial mon:stdio -drive if=pflash,format=raw,readonly=on,file=$(OVMF)

check-bootloader: bootloader-iso
	python3 scripts/check-bootloader.py
