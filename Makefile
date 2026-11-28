include toolchain.mk

.DEFAULT_GOAL := iso
QEMU := qemu-system-x86_64
QEMUFLAGS := -machine q35 -accel tcg -m 256M -boot d -net none
OVMF ?= /usr/share/OVMF/OVMF_CODE_4M.fd
DISPLAY_BACKEND ?= gtk
KERNEL_OBJECTS := build/kernel/entry.o build/kernel/main.o build/kernel/console.o build/kernel/serial.o build/kernel/log.o build/kernel/panic.o build/kernel/interrupts.o build/kernel/interrupt_stubs.o build/kernel/keyboard.o build/kernel/terminal.o build/kernel/power.o build/kernel/acpi_s5.o build/kernel/memory.o build/kernel/pmm_core.o build/kernel/pmm.o build/kernel/vmm_core.o build/kernel/vmm.o build/kernel/heap_core.o build/kernel/heap.o

KERNEL_OBJECTS += build/kernel/memtest.o
KERNEL_OBJECTS += build/kernel/process_core.o build/kernel/process.o build/kernel/process_switch.o
KERNEL_OBJECTS += build/kernel/runtime.o
KERNEL_OBJECTS += build/kernel/resource_core.o build/kernel/process_resources.o
KERNEL_OBJECTS += build/kernel/address_space_core.o build/kernel/address_space.o
KERNEL_OBJECTS += build/kernel/user_mode.o
KERNEL_OBJECTS += build/kernel/syscall.o
KERNEL_OBJECTS += build/kernel/elf64.o
KERNEL_OBJECTS += build/kernel/programs.o

.PHONY: check-toolchain
check-toolchain:
	bash scripts/check-toolchain.sh "$(CC)" "$(AS)" "$(LD)" "$(ARCH)" "$(CFLAGS)" "$(ASFLAGS)" "$(LDFLAGS)"

.PHONY: setup-limine kernel iso run run-uefi check-kernel check-kernel-arch
setup-limine:
	bash scripts/setup-limine.sh

KERNEL_CFLAGS := $(CFLAGS) -mcmodel=kernel -fno-asynchronous-unwind-tables -fno-unwind-tables -O2 -Ikernel/include -Ibuild/include -MMD -MP

kernel: build/kernel.elf

check-kernel-arch:
	@test "$(ARCH)" = x86_64 || { echo "O kernel inicial suporta apenas ARCH=x86_64."; exit 1; }

build/include/version.h: VERSION Makefile
	mkdir -p $(@D)
	printf '#define NEXUS_VERSION "%s"\n' "$$(tr -d '\r\n' < VERSION)" > $@

build/kernel/%.o: kernel/%.c toolchain.mk Makefile build/include/version.h | check-kernel-arch
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/kernel/entry.o: kernel/entry.asm toolchain.mk Makefile | check-kernel-arch
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

build/kernel/interrupt_stubs.o: kernel/interrupts.asm toolchain.mk Makefile | check-kernel-arch
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

build/kernel/process_switch.o: kernel/process_switch.asm toolchain.mk Makefile | check-kernel-arch
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

build/include/syscall_abi.inc: kernel/include/syscall_abi.h scripts/gen-syscall-abi.py
	mkdir -p $(@D)
	python3 scripts/gen-syscall-abi.py $< $@

USER_CFLAGS := -std=c11 -ffreestanding -fno-stack-protector -fno-pie -fno-pic -fno-asynchronous-unwind-tables -fno-unwind-tables -m64 -mno-red-zone -mgeneral-regs-only -Os -Wall -Wextra -Werror -Iuser/include -Ikernel/include

build/user/%.o: user/%.c user/include/syscall.h kernel/include/syscall_abi.h Makefile
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

build/user/elf_start.o: user/elf_start.asm build/include/syscall_abi.inc Makefile
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) -Ibuild/include/ $< -o $@

build/user/elf_alpha.o build/user/elf_beta.o: user/include/elf_runtime.h

build/user/alpha.elf: build/user/elf_alpha.o build/user/elf_start.o user/elf_linker.ld
	$(LD) -nostdlib -m elf_x86_64 -z max-page-size=0x1000 -T user/elf_linker.ld build/user/elf_start.o build/user/elf_alpha.o -o $@

build/user/beta.elf: build/user/elf_beta.o build/user/elf_start.o user/elf_linker.ld
	$(LD) -nostdlib -m elf_x86_64 -z max-page-size=0x1000 -T user/elf_linker.ld build/user/elf_start.o build/user/elf_beta.o -o $@

build/user/syscall_registers.o: user/syscall_registers.asm build/include/syscall_abi.inc Makefile
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) -Ibuild/include/ $< -o $@

build/user/syscall-test.elf: build/user/syscall_test.o build/user/syscall_registers.o user/linker.ld
	$(LD) -nostdlib -m elf_x86_64 -T user/linker.ld build/user/syscall_test.o build/user/syscall_registers.o -o $@

build/user/syscall-test.bin: build/user/syscall-test.elf
	objcopy -O binary $< $@

build/user/read-test.elf: build/user/read_test.o user/linker.ld
	$(LD) -nostdlib -m elf_x86_64 -T user/linker.ld build/user/read_test.o -o $@

build/user/read-test.bin: build/user/read-test.elf
	objcopy -O binary $< $@

build/kernel/user_mode.o: kernel/user_mode.asm toolchain.mk Makefile build/include/syscall_abi.inc build/user/syscall-test.bin build/user/read-test.bin | check-kernel-arch
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) -Ibuild/include/ $< -o $@

build/kernel.elf: $(KERNEL_OBJECTS) kernel/linker.ld | check-kernel-arch
	$(LD) $(LDFLAGS) -z max-page-size=0x1000 -T kernel/linker.ld $(KERNEL_OBJECTS) -o $@

iso: kernel setup-limine build/user/alpha.elf build/user/beta.elf
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

.PHONY: check-terminal
check-terminal: iso
	python3 scripts/check-kernel.py --terminal

.PHONY: check-power check-acpi
check-acpi:
	mkdir -p build/tests
	gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ikernel/include tests/acpi_s5_test.c kernel/acpi_s5.c -o build/tests/acpi-s5
	build/tests/acpi-s5

check-power: iso check-acpi
	python3 scripts/check-power.py

.PHONY: check-memory check-memory-unit
check-memory-unit:
	mkdir -p build/tests
	gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ikernel/include tests/memory_test.c kernel/memory.c -o build/tests/memory
	build/tests/memory

check-memory: iso check-memory-unit
	python3 scripts/check-kernel.py --memory

.PHONY: check-pmm check-pmm-unit
check-pmm-unit:
	mkdir -p build/tests
	gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ikernel/include tests/pmm_test.c kernel/pmm_core.c kernel/memory.c -o build/tests/pmm
	build/tests/pmm

check-pmm: iso check-pmm-unit
	python3 scripts/check-kernel.py --pmm

.PHONY: check-vmm check-vmm-unit
check-vmm-unit:
	mkdir -p build/tests
	gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ikernel/include tests/vmm_test.c kernel/vmm_core.c -o build/tests/vmm
	build/tests/vmm

check-vmm: iso check-vmm-unit
	python3 scripts/check-kernel.py --vmm

.PHONY: check-heap check-heap-unit
check-heap-unit:
	mkdir -p build/tests
	gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ikernel/include tests/heap_test.c kernel/heap_core.c -o build/tests/heap
	build/tests/heap

check-heap: iso check-heap-unit
	python3 scripts/check-kernel.py --heap

.PHONY: check-access
check-access: iso check-heap-unit
	python3 scripts/check-kernel.py --access

.PHONY: check-memtest
check-memtest: iso
	python3 scripts/check-kernel.py --memtest

.PHONY: check-processes check-processes-unit
check-processes-unit:
	mkdir -p build/tests
	gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ikernel/include tests/process_test.c kernel/process_core.c -o build/tests/process
	build/tests/process

check-processes: iso check-processes-unit check-resources-unit
	python3 scripts/check-kernel.py --processes

.PHONY: check-resources-unit
check-resources-unit:
	mkdir -p build/tests
	gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ikernel/include tests/resource_test.c kernel/resource_core.c -o build/tests/resources
	build/tests/resources

.PHONY: check-scheduler check-scheduler-unit
check-scheduler-unit:
	mkdir -p build/tests
	gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ikernel/include tests/scheduler_test.c kernel/process_core.c -o build/tests/scheduler
	build/tests/scheduler

check-scheduler: iso check-scheduler-unit check-processes-unit check-resources-unit
	python3 scripts/check-kernel.py --scheduler

.PHONY: check-address-space-unit
check-address-space-unit:
	mkdir -p build/tests
	gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ikernel/include tests/address_space_test.c kernel/address_space_core.c -o build/tests/address-space
	build/tests/address-space

.PHONY: check-user
check-user: iso check-address-space-unit
	python3 scripts/check-kernel.py --user

.PHONY: check-syscalls
check-syscalls: iso check-address-space-unit
	python3 scripts/check-kernel.py --syscalls

.PHONY: check-elf-unit
check-elf-unit:
	mkdir -p build/tests
	gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ikernel/include tests/elf64_test.c kernel/elf64.c kernel/address_space_core.c -o build/tests/elf64
	build/tests/elf64

.PHONY: check-elf
check-elf: iso check-elf-unit
	python3 scripts/check-kernel.py --elf

# Compatibilidade com os comandos da etapa anterior: agora incluem o kernel.
.PHONY: bootloader-iso run-bootloader run-bootloader-uefi check-bootloader
bootloader-iso: iso
run-bootloader: run
run-bootloader-uefi: run-uefi
check-bootloader: check-kernel

-include $(wildcard build/kernel/*.d)
