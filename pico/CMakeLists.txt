cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "E:/dev/pico-sdk")

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(pico_xyzt C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(pico_xyzt
    main.c
)

pico_generate_pio_header(pico_xyzt ${CMAKE_CURRENT_LIST_DIR}/sampler.pio)

target_link_libraries(pico_xyzt
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_clocks
    pico_unique_id
)

# USB serial output (not UART)
pico_enable_stdio_usb(pico_xyzt 1)
pico_enable_stdio_uart(pico_xyzt 0)

pico_add_extra_outputs(pico_xyzt)
