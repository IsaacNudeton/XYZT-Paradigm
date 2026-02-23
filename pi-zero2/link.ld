/* XYZT-OS Linker Script - Pi Zero 2 bare metal
 *
 * Memory map:
 *   0x00000 - 0x7FFFF : Stack (grows down from 0x80000)
 *   0x80000 - ...      : Kernel code + data
 *   0x3F000000         : Peripheral MMIO base (BCM2837)
 *
 * Position is value. Address IS identity.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x80000;

    .text : {
        KEEP(*(.text.boot))     /* Boot code MUST be first */
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    . = ALIGN(16);
    __bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    __bss_end = .;

    /* Heap starts after BSS - for protocol library storage */
    . = ALIGN(4096);
    __heap_start = .;
}
