OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(5); }
SECTIONS {
    . = 0x400000;
    .text : { KEEP(*(.text.entry)) *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) } :image
    .unsupported : { *(.data .data.*) *(.bss .bss.*) *(COMMON) } :image
    ASSERT(SIZEOF(.unsupported) == 0, "Imagem plana: use stack/dados em 0x600000, sem .data/.bss")
    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) *(.comment) }
}
