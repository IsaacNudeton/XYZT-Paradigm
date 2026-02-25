/*
 * main.c — XYZT Pico Firmware
 *
 * Power hits the board. Four things happen:
 *   1. T: The crystal is ticking (125MHz hardware clock)
 *   2. X/Y: PIO0 retina samples raw copper at system clock
 *   3. Discovery: the device learns its own body
 *   4. Z: CPU enters permanent observation loop
 *
 * GPIO 28 is the telemetry tap. PIO1 shifts the wire graph
 * out at 2Mbps via DMA. One row per tick. Full matrix every
 * 128ms. Zero CPU overhead. The observer never knows.
 *
 * Boot sequence:
 *   - Retina opens (all pins input, PIO sampling)
 *   - Listen: who's already talking? (world pins)
 *   - Probe: drive each quiet pin, see what responds (self links)
 *   - Classify: what's left is available for output (limbs)
 *   - Observe forever
 *
 * Every device runs the same firmware. Same soul, different body.
 * Connect two together: their wire graphs converge through
 * shared signals. They become one. Not by protocol. By co-presence.
 *
 * Connect to regular hardware: the device learns its structure.
 * Adapts to speak its language. But stays separate — the other
 * side doesn't adapt back.
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

    /* Discovery: learn what I am before I observe the world */
    hw_discover_body();

    capture_buf_t buf;

    /* Z: The observer digests reality forever */
    while (1) {
        capture_for_duration(&buf, 1000);
        sense_observe(&buf, 1000);
    }
}
