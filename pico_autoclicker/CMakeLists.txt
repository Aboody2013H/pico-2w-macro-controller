cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_bootsel_autoclicker C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico_bootsel_autoclicker
    src/main.cpp
    src/usb_descriptors.c
)

target_include_directories(pico_bootsel_autoclicker PRIVATE src)

target_compile_definitions(pico_bootsel_autoclicker PRIVATE
    CFG_TUSB_CONFIG_FILE="tusb_config.h"
)

if(PICO_BOARD STREQUAL "pico2_w")
    target_compile_definitions(pico_bootsel_autoclicker PRIVATE AUTOCLICKER_PICO2W=1)
    target_link_libraries(pico_bootsel_autoclicker PRIVATE pico_cyw43_arch_none)
endif()

target_compile_options(pico_bootsel_autoclicker PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wall -Wextra -Wpedantic>
)

target_link_libraries(pico_bootsel_autoclicker PRIVATE
    pico_stdlib
    tinyusb_device
    hardware_sync
)

pico_enable_stdio_usb(pico_bootsel_autoclicker 0)
pico_enable_stdio_uart(pico_bootsel_autoclicker 0)
pico_set_program_name(pico_bootsel_autoclicker "Pico BOOTSEL Autoclicker")
pico_set_program_version(pico_bootsel_autoclicker "1.0.0")
pico_add_extra_outputs(pico_bootsel_autoclicker)
