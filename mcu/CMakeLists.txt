cmake_minimum_required(VERSION 3.13)

# Locate the Pico SDK. Honour PICO_SDK_PATH from the environment/cache if set;
# otherwise default to a 'pico-sdk' checkout at the repository root (../pico-sdk).
if(NOT DEFINED PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../pico-sdk")
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(criptolite_rp C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(criptolite_rp
    main.c
    usb_descriptors.c
    spi_pio.c
    spi_flash.c
    ice40.c
    trng.c
    sha256.c
    nist_health.c
    rng_pipeline.c
    flash_writer.c
    led_status.c
    protocol.c
)

# Generate PIO header (only spi_pio.pio — no PIO sampler in this firmware)
pico_generate_pio_header(criptolite_rp
    ${CMAKE_CURRENT_SOURCE_DIR}/spi_pio.pio
)

# tusb_config.h must be visible to TinyUSB
target_include_directories(criptolite_rp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(criptolite_rp
    pico_stdlib
    pico_unique_id
    pico_bootrom
    tinyusb_device
    tinyusb_board
    hardware_pio
    hardware_clocks
    hardware_gpio
    hardware_timer
)

# Do not use SDK stdio — we drive USB ourselves via TinyUSB
pico_enable_stdio_usb(criptolite_rp 0)
pico_enable_stdio_uart(criptolite_rp 0)

# Generate .uf2 (and .bin, .hex, .map, .dis)
pico_add_extra_outputs(criptolite_rp)
