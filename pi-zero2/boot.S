// XYZT-OS Boot - Pi Zero 2 (BCM2710A1 / Cortex-A53)
// AArch64 bare metal entry point
//
// GPU loads this at 0x80000 and releases ARM core 0.
// We park cores 1-3, set up stack, jump to C.
// Position is value. This code IS the substrate initialization.

.section ".text.boot"
.global _start

_start:
    // Which core are we? Only core 0 proceeds.
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbz     x0, core0_boot

    // Cores 1-3: spin in low-power wait
    // Later we can wake them for parallel XYZT collision processing
park_core:
    wfe
    b       park_core

core0_boot:
    // Drop from EL2 to EL1 if needed (firmware may start us in EL2)
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ne    skip_el2

    // Configure EL1 from EL2
    mov     x0, #(1 << 31)         // AArch64 at EL1
    msr     hcr_el2, x0

    mov     x0, #0x3C5             // EL1h, DAIF masked
    msr     spsr_el2, x0

    adr     x0, skip_el2
    msr     elr_el2, x0
    eret

skip_el2:
    // Set up stack pointer - grows down from 0x80000
    // Our kernel is loaded AT 0x80000, stack goes below
    ldr     x0, =_start
    mov     sp, x0

    // Clear BSS
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
bss_clear:
    cmp     x0, x1
    b.ge    bss_done
    str     xzr, [x0], #8
    b       bss_clear
bss_done:

    // Jump to XYZT kernel main
    bl      xyzt_kernel_main

    // If kernel returns, halt
halt:
    wfe
    b       halt
