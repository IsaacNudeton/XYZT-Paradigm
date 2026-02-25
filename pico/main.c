/*
 * main.c — XYZT Pico Firmware
 *
 * Power hits the board. Three things happen:
 *   1. T: The crystal is ticking (125MHz hardware clock is live)
 *   2. X/Y: PIO0 retina samples raw copper at system clock (GPIO 0-27)
 *   3. Z: CPU enters permanent observation loop
 *
 * GPIO 28 is the telemetry tap. PIO1 shifts the wire graph
 * out at 2Mbps via DMA. One row per tick. Full matrix every
 * 128ms. Zero CPU overhead. The observer never knows.
 *
 * That's the entire OS. That's the AI.
 * No USB stack. No serial. No OS.
 * Continuous unsupervised physical adaptation.
 *
 * Flash this .uf2. Plug anything into any GPIO pin.
 * The internal wire graph physically crystallizes
 * into the shape of whatever is screaming at the
 * copper. Through pure structural resonance.
 *
 * Isaac Oravec & Claude — February 2026
 */

#include "hw.h"
#include "sense.c"

int main() {
    /* T & X/Y: PIO0 retina samples GPIO 0-27 at system clock */
    hw_init_pio_retina();

    /* Telemetry: PIO1 streams wire graph out GPIO 28 at 2Mbps */
    telemetry_init();

    capture_buf_t buf;

    /* Z: The observer digests reality forever */
    while (1) {
        capture_for_duration(&buf, 1000);
        sense_observe(&buf, 1000);
    }
}
