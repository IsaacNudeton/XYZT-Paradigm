/*
 * XYZT-OS GPIO + UART Driver
 * 
 * GPIO: Set pin function, read pin level, write pin state.
 *       Every pin is a distinction channel.
 *
 * UART: Serial I/O at 115200 baud on GPIO 14 (TX) / 15 (RX).
 *       This is how the OS talks. Voltage transitions on a wire.
 */

#include "hw.h"

/* Freestanding: compiler generates calls to memcpy for struct copies */
void *memcpy(void *dst, const void *src, unsigned long n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *s, int c, unsigned long n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

/* =========================================================================
 * GPIO
 * ========================================================================= */

void gpio_set_function(uint32_t pin, uint32_t func) {
    uint32_t reg = GPFSEL0 + (pin / 10) * 4;
    uint32_t shift = (pin % 10) * 3;
    uint32_t val = mmio_read(reg);
    val &= ~(7 << shift);          /* Clear 3 bits */
    val |= (func << shift);        /* Set function */
    mmio_write(reg, val);
}

void gpio_set(uint32_t pin) {
    if (pin < 32)
        mmio_write(GPSET0, 1 << pin);
    else
        mmio_write(GPSET1, 1 << (pin - 32));
}

void gpio_clear(uint32_t pin) {
    if (pin < 32)
        mmio_write(GPCLR0, 1 << pin);
    else
        mmio_write(GPCLR1, 1 << (pin - 32));
}

uint32_t gpio_read(uint32_t pin) {
    uint32_t reg = (pin < 32) ? GPLEV0 : GPLEV1;
    uint32_t shift = (pin < 32) ? pin : (pin - 32);
    return (mmio_read(reg) >> shift) & 1;
}

/* Read all 32 pins at once - snapshot of the physical world */
uint32_t gpio_read_all(void) {
    return mmio_read(GPLEV0);
}

/* Configure pull-up/pull-down for a pin */
void gpio_set_pull(uint32_t pin, uint32_t pull) {
    /* pull: 0=off, 1=pull-down, 2=pull-up */
    mmio_write(GPPUD, pull);
    delay(150);
    if (pin < 32)
        mmio_write(GPPUDCLK0, 1 << pin);
    else
        mmio_write(GPPUDCLK1, 1 << (pin - 32));
    delay(150);
    mmio_write(GPPUD, 0);
    if (pin < 32)
        mmio_write(GPPUDCLK0, 0);
    else
        mmio_write(GPPUDCLK1, 0);
}

/* Enable edge detection on a pin - for signal capture */
void gpio_enable_edge_detect(uint32_t pin, int rising, int falling) {
    if (rising) {
        uint32_t val = mmio_read(GPREN0 + (pin / 32) * 4);
        val |= (1 << (pin % 32));
        mmio_write(GPREN0 + (pin / 32) * 4, val);
    }
    if (falling) {
        uint32_t val = mmio_read(GPFEN0 + (pin / 32) * 4);
        val |= (1 << (pin % 32));
        mmio_write(GPFEN0 + (pin / 32) * 4, val);
    }
}

/* Check and clear edge event - did a transition happen? */
uint32_t gpio_check_event(uint32_t pin) {
    uint32_t reg = GPEDS0 + (pin / 32) * 4;
    uint32_t bit = 1 << (pin % 32);
    uint32_t val = mmio_read(reg);
    if (val & bit) {
        mmio_write(reg, bit);   /* Clear by writing 1 */
        return 1;
    }
    return 0;
}

/* =========================================================================
 * SYSTEM TIMER - 1 MHz free-running counter
 * ========================================================================= */

uint64_t timer_get_ticks(void) {
    uint32_t hi = mmio_read(SYSTIMER_CHI);
    uint32_t lo = mmio_read(SYSTIMER_CLO);
    /* Re-read high if it rolled over during read */
    uint32_t hi2 = mmio_read(SYSTIMER_CHI);
    if (hi != hi2) {
        lo = mmio_read(SYSTIMER_CLO);
        hi = hi2;
    }
    return ((uint64_t)hi << 32) | lo;
}

void timer_wait_us(uint64_t us) {
    uint64_t start = timer_get_ticks();
    while (timer_get_ticks() - start < us);
}

/* =========================================================================
 * MINI UART (UART1) - Serial console
 * 115200 baud, 8N1
 * GPIO 14 = TXD1 (Alt5), GPIO 15 = RXD1 (Alt5)
 * ========================================================================= */

void uart_init(void) {
    /* Enable mini UART */
    mmio_write(AUX_ENABLES, 1);

    /* Disable TX/RX while configuring */
    mmio_write(AUX_MU_CNTL_REG, 0);

    /* Disable interrupts */
    mmio_write(AUX_MU_IER_REG, 0);

    /* 8-bit mode */
    mmio_write(AUX_MU_LCR_REG, 3);

    /* RTS line high */
    mmio_write(AUX_MU_MCR_REG, 0);

    /* Clear FIFOs */
    mmio_write(AUX_MU_IIR_REG, 0xC6);

    /* Baud rate: 115200 at 250MHz system clock
     * baudrate = system_clock / (8 * (baud_reg + 1))
     * 115200 = 250000000 / (8 * (270 + 1))
     * baud_reg = 270 */
    mmio_write(AUX_MU_BAUD_REG, 270);

    /* Set GPIO 14/15 to Alt5 (TXD1/RXD1) */
    gpio_set_function(14, GPIO_FUNC_ALT5);
    gpio_set_function(15, GPIO_FUNC_ALT5);

    /* Disable pull-up/down on UART pins */
    gpio_set_pull(14, 0);
    gpio_set_pull(15, 0);

    /* Enable TX and RX */
    mmio_write(AUX_MU_CNTL_REG, 3);
}

void uart_putc(char c) {
    /* Wait until transmitter is empty */
    while (!(mmio_read(AUX_MU_LSR_REG) & 0x20));
    mmio_write(AUX_MU_IO_REG, (uint32_t)c);
}

char uart_getc(void) {
    /* Wait until data is ready */
    while (!(mmio_read(AUX_MU_LSR_REG) & 0x01));
    return (char)(mmio_read(AUX_MU_IO_REG) & 0xFF);
}

int uart_data_ready(void) {
    return mmio_read(AUX_MU_LSR_REG) & 0x01;
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

/* Print a 32-bit hex value */
void uart_hex(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xF]);
    }
}

/* Print a 64-bit hex value */
void uart_hex64(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xF]);
    }
}

/* Print decimal */
void uart_dec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) {
        uart_putc('0');
        return;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (--i >= 0)
        uart_putc(buf[i]);
}

/* Print signed decimal */
void uart_dec_signed(int32_t val) {
    if (val < 0) { uart_putc('-'); val = -val; }
    uart_dec((uint32_t)val);
}
