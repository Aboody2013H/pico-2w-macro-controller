cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Board type")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_2w_macro_native C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(CIRCUITPY_SETTINGS "D:/settings.toml" CACHE FILEPATH "CircuitPython settings.toml used to seed Wi-Fi credentials")
if(NOT EXISTS "${CIRCUITPY_SETTINGS}")
    message(FATAL_ERROR "Missing ${CIRCUITPY_SETTINGS}. Pass -DCIRCUITPY_SETTINGS=path/to/settings.toml")
endif()
file(READ "${CIRCUITPY_SETTINGS}" SETTINGS_TEXT)

function(read_toml_string KEY OUTPUT)
    string(REGEX MATCH "(^|\n)[ \t]*${KEY}[ \t]*=[ \t]*\"([^\"]*)\"" _match "${SETTINGS_TEXT}")
    if(NOT _match)
        message(FATAL_ERROR "${KEY} is missing from ${CIRCUITPY_SETTINGS}")
    endif()
    set(_value "${CMAKE_MATCH_2}")
    string(REPLACE "\\" "\\\\" _value "${_value}")
    string(REPLACE "\"" "\\\"" _value "${_value}")
    set(${OUTPUT} "${_value}" PARENT_SCOPE)
endfunction()

foreach(N RANGE 1 3)
    read_toml_string("WIFI${N}_SSID" WIFI${N}_SSID_VALUE)
    read_toml_string("WIFI${N}_PASSWORD" WIFI${N}_PASSWORD_VALUE)
endforeach()

configure_file(src/wifi_config.h.in generated/wifi_config.h @ONLY)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/web")
configure_file("${CMAKE_CURRENT_LIST_DIR}/../outputs/index.html" "${CMAKE_CURRENT_BINARY_DIR}/web/index.html" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../outputs/builder.html" "${CMAKE_CURRENT_BINARY_DIR}/web/builder" COPYONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/web/ok.txt" "OK")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/web/404.html" "<!doctype html><title>404</title><h1>Not found</h1>")

pico_add_library(pico_macro_web NOFLAG)
pico_set_lwip_httpd_content(pico_macro_web INTERFACE
    "${CMAKE_CURRENT_BINARY_DIR}/web/index.html"
    "${CMAKE_CURRENT_BINARY_DIR}/web/builder"
    "${CMAKE_CURRENT_BINARY_DIR}/web/ok.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/web/404.html"
)

add_executable(pico_2w_macro
    src/main.cpp
    src/macro_engine.cpp
    src/storage.cpp
    src/web_api.cpp
    src/wifi_manager.cpp
    src/usb_descriptors.c
)

target_include_directories(pico_2w_macro PRIVATE
    src
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${PICO_LWIP_CONTRIB_PATH}/apps/httpd"
)

target_compile_definitions(pico_2w_macro PRIVATE
    CFG_TUSB_CONFIG_FILE="tusb_config.h"
    PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=1
    PICO_STDIO_USB=1
    PICO_STDIO_UART=1
)

target_compile_options(pico_2w_macro PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wall -Wextra -Wpedantic>
)

target_link_libraries(pico_2w_macro PRIVATE
    pico_stdlib
    pico_unique_id
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_http
    pico_lwip_mdns
    pico_macro_web
    tinyusb_device
    pico_stdio_usb
    hardware_flash
    hardware_sync
)

pico_enable_stdio_usb(pico_2w_macro 1)
pico_enable_stdio_uart(pico_2w_macro 1)
pico_set_program_name(pico_2w_macro "Pico 2W Macro Native")
pico_set_program_version(pico_2w_macro "1.0.0")
pico_add_extra_outputs(pico_2w_macro)
