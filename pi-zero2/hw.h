/*
 * XYZT-OS Hardware Abstraction
 * BCM2710A1 / BCM2837 register map for Pi Zero 2
 *
 * Every register is a memory address. Every address is a position.
 * Position is value. The peripheral map IS the hardware's identity.
 */

#ifndef XYZT_HW_H
#define XYZT_HW_H

#include <stdint.h>

/* =========================================================================
 * MMIO BASE - BCM2837 peripheral base address
 * The BCM2835 manual says 0x7E000000 (bus address)
 * ARM physical address = 0x3F000000
 * ========================================================================= */
#define PERIPHERAL_BASE     0x3F000000

/* Helper: read/write 32-bit MMIO registers */
static inline void mmio_write(uint64_t reg, uint32_t val) {
    *(volatile uint32_t *)reg = val;
}

static inline uint32_t mmio_read(uint64_t reg) {
    return *(volatile uint32_t *)reg;
}

/* Delay loop - no timers yet, just burn cycles */
static inline void delay(uint32_t count) {
    while (count--) {
        asm volatile("nop");
    }
}

/* =========================================================================
 * GPIO - General Purpose I/O
 * Base: 0x3F200000
 * 54 GPIO pins, each with function select, set, clear, level, detect
 *
 * THIS is the nervous system. Every pin is a distinction channel.
 * ========================================================================= */
#define GPIO_BASE           (PERIPHERAL_BASE + 0x200000)

/* Function select: 3 bits per pin, 10 pins per register */
#define GPFSEL0             (GPIO_BASE + 0x00)  /* GPIO 0-9   */
#define GPFSEL1             (GPIO_BASE + 0x04)  /* GPIO 10-19 */
#define GPFSEL2             (GPIO_BASE + 0x08)  /* GPIO 20-29 */
#define GPFSEL3             (GPIO_BASE + 0x0C)  /* GPIO 30-39 */
#define GPFSEL4             (GPIO_BASE + 0x10)  /* GPIO 40-49 */
#define GPFSEL5             (GPIO_BASE + 0x14)  /* GPIO 50-53 */

/* Pin output set (write 1 = set high) */
#define GPSET0              (GPIO_BASE + 0x1C)  /* GPIO 0-31  */
#define GPSET1              (GPIO_BASE + 0x20)  /* GPIO 32-53 */

/* Pin output clear (write 1 = set low) */
#define GPCLR0              (GPIO_BASE + 0x28)  /* GPIO 0-31  */
#define GPCLR1              (GPIO_BASE + 0x2C)  /* GPIO 32-53 */

/* Pin level (read: current state) */
#define GPLEV0              (GPIO_BASE + 0x34)  /* GPIO 0-31  */
#define GPLEV1              (GPIO_BASE + 0x38)  /* GPIO 32-53 */

/* Event detect status */
#define GPEDS0              (GPIO_BASE + 0x40)
#define GPEDS1              (GPIO_BASE + 0x44)

/* Rising/falling edge detect enable */
#define GPREN0              (GPIO_BASE + 0x4C)
#define GPFEN0              (GPIO_BASE + 0x58)

/* Pull-up/pull-down */
#define GPPUD               (GPIO_BASE + 0x94)
#define GPPUDCLK0           (GPIO_BASE + 0x98)
#define GPPUDCLK1           (GPIO_BASE + 0x9C)

/* GPIO function select values */
#define GPIO_FUNC_INPUT     0
#define GPIO_FUNC_OUTPUT    1
#define GPIO_FUNC_ALT0      4
#define GPIO_FUNC_ALT1      5
#define GPIO_FUNC_ALT2      6
#define GPIO_FUNC_ALT3      7
#define GPIO_FUNC_ALT4      3
#define GPIO_FUNC_ALT5      2

/* =========================================================================
 * MINI UART (UART1) - Serial communication
 * Base: 0x3F215000 (AUX peripherals)
 * Used for: console I/O, communication with host PC via USB-serial
 *
 * This is the voice. UART converts register values to voltage transitions.
 * Voltage transitions ARE distinctions on a wire.
 * ========================================================================= */
#define AUX_BASE            (PERIPHERAL_BASE + 0x215000)

#define AUX_IRQ             (AUX_BASE + 0x00)
#define AUX_ENABLES         (AUX_BASE + 0x04)

#define AUX_MU_IO_REG       (AUX_BASE + 0x40)  /* Data read/write */
#define AUX_MU_IER_REG      (AUX_BASE + 0x44)  /* Interrupt enable */
#define AUX_MU_IIR_REG      (AUX_BASE + 0x48)  /* Interrupt status */
#define AUX_MU_LCR_REG      (AUX_BASE + 0x4C)  /* Line control */
#define AUX_MU_MCR_REG      (AUX_BASE + 0x50)  /* Modem control */
#define AUX_MU_LSR_REG      (AUX_BASE + 0x54)  /* Line status */
#define AUX_MU_MSR_REG      (AUX_BASE + 0x58)  /* Modem status */
#define AUX_MU_SCRATCH       (AUX_BASE + 0x5C)  /* Scratch */
#define AUX_MU_CNTL_REG     (AUX_BASE + 0x60)  /* Extra control */
#define AUX_MU_STAT_REG     (AUX_BASE + 0x64)  /* Extra status */
#define AUX_MU_BAUD_REG     (AUX_BASE + 0x68)  /* Baudrate */

/* =========================================================================
 * SYSTEM TIMER - Free-running 1 MHz counter
 * Base: 0x3F003000
 * Used for: timestamping signal edges, measuring bit periods
 * ========================================================================= */
#define SYSTIMER_BASE       (PERIPHERAL_BASE + 0x003000)

#define SYSTIMER_CS         (SYSTIMER_BASE + 0x00)  /* Control/status */
#define SYSTIMER_CLO        (SYSTIMER_BASE + 0x04)  /* Counter low 32 bits */
#define SYSTIMER_CHI        (SYSTIMER_BASE + 0x08)  /* Counter high 32 bits */
#define SYSTIMER_C0         (SYSTIMER_BASE + 0x0C)  /* Compare 0 */
#define SYSTIMER_C1         (SYSTIMER_BASE + 0x10)  /* Compare 1 */
#define SYSTIMER_C2         (SYSTIMER_BASE + 0x14)  /* Compare 2 */
#define SYSTIMER_C3         (SYSTIMER_BASE + 0x18)  /* Compare 3 */

/* =========================================================================
 * SPI - Serial Peripheral Interface
 * Base: 0x3F204000
 * Used for: FT232H communication, FPGA programming
 * ========================================================================= */
#define SPI0_BASE           (PERIPHERAL_BASE + 0x204000)

#define SPI0_CS             (SPI0_BASE + 0x00)  /* Control and status */
#define SPI0_FIFO           (SPI0_BASE + 0x04)  /* TX/RX FIFO */
#define SPI0_CLK            (SPI0_BASE + 0x08)  /* Clock divider */
#define SPI0_DLEN           (SPI0_BASE + 0x0C)  /* Data length */

/* =========================================================================
 * I2C (BSC) - Two-wire interface
 * Base: BSC0=0x3F205000, BSC1=0x3F804000
 * Two things need a substrate to connect. I2C = SDA + SCL + GND.
 * {2,3} in a wire.
 * ========================================================================= */
#define BSC0_BASE           (PERIPHERAL_BASE + 0x205000)
#define BSC1_BASE           (PERIPHERAL_BASE + 0x804000)

#define BSC_C(base)         ((base) + 0x00)  /* Control */
#define BSC_S(base)         ((base) + 0x04)  /* Status */
#define BSC_DLEN(base)      ((base) + 0x08)  /* Data length */
#define BSC_A(base)         ((base) + 0x0C)  /* Slave address */
#define BSC_FIFO(base)      ((base) + 0x10)  /* Data FIFO */

/* =========================================================================
 * MAILBOX - GPU communication
 * Used for: clock setup, framebuffer, power management
 * ========================================================================= */
#define MAILBOX_BASE        (PERIPHERAL_BASE + 0x00B880)

#define MAILBOX_READ        (MAILBOX_BASE + 0x00)
#define MAILBOX_STATUS      (MAILBOX_BASE + 0x18)
#define MAILBOX_WRITE       (MAILBOX_BASE + 0x20)

#define MAILBOX_FULL        0x80000000
#define MAILBOX_EMPTY       0x40000000

#endif /* XYZT_HW_H */
