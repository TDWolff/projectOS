ENTRY(_start)
OUTPUT_FORMAT(binary)

SECTIONS {
    . = 0x100000000; /* Move to 4GB to avoid identity map territory */
    .text : {
        *(.text)
    }
    .rodata : {
        *(.rodata)
    }
}