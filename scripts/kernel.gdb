set pagination off
set confirm off
set architecture i386:x86-64
set disassembly-flavor intel
file build/kernel.elf
target remote 127.0.0.1:1234
hbreak *kmain
continue
printf "Entrada C: RIP=%p RSP=%p (RSP mod 16 deve ser 8)\n", $rip, $rsp
info registers
