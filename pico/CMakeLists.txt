cmake_minimum_required(VERSION 3.13)

# Point to pico-sdk (set PICO_SDK_PATH env var or edit this)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(xyzt_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(xyzt_pico main.c)

# Bare metal. No USB stack. No stdio. No OS.
# Only hardware libraries the silicon needs.
target_link_libraries(xyzt_pico
    pico_stdlib          # minimal runtime (startup, clocks, no stdio used)
    hardware_pio
    hardware_dma
    hardware_gpio
    hardware_timer
    hardware_clocks
)

# Disable ALL stdio — no USB serial, no UART serial. Nothing.
pico_enable_stdio_usb(xyzt_pico 0)
pico_enable_stdio_uart(xyzt_pico 0)

# Generate .uf2 for drag-and-drop flash
pico_add_uf2_output(xyzt_pico)

# That's it. cmake --build build → xyzt_pico.uf2
# Hold BOOTSEL, plug Pico, drag .uf2 onto the drive.
# Board is alive.
