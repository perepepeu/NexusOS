# Compilacao freestanding: sem bibliotecas ou inicializacao do Linux.
ARCH ?= x86_64
CC := gcc
AS := nasm
LD := ld
OBJCOPY := objcopy

CFLAGS := -std=c11 -ffreestanding -fno-stack-protector -fno-pie -fno-pic -Wall -Wextra -Werror -g
LDFLAGS := -nostdlib

ifeq ($(ARCH),x86_64)
CFLAGS += -m64 -mno-red-zone -mgeneral-regs-only
ASFLAGS := -f elf64 -g -F dwarf
LDFLAGS += -m elf_x86_64
else ifeq ($(ARCH),i686)
CFLAGS += -m32 -march=i686 -mgeneral-regs-only
ASFLAGS := -f elf32 -g -F dwarf
LDFLAGS += -m elf_i386
else
$(error ARCH deve ser x86_64 ou i686)
endif
