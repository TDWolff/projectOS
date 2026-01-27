ENTRY(start)

SECTIONS {
    . = 1M;

    .text : {
        *(.text)
    }

    /* Align to 4KB blocks to be safe */
    . = ALIGN(4096);

    .rodata : {
        *(.rodata)
    }

    . = ALIGN(4096);

    .data : {
        *(.data)
    }

    . = ALIGN(4096);

    .bss : {
        *(.bss)
    }
}