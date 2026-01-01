 cmake_minimum_required(VERSION 3.12)

set(PFB_AES_KEY "DickinsAudio2025Firmware" CACHE STRING "AES key for PFB")


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
message(STATUS "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")

set(BOARD_NAME W5500_EVB_PICO2)
set(PICO_PLATFORM rp2350)
set(WIZNET_CHIP W5500)
add_definitions(-D_WIZCHIP_=W5500)
add_definitions(-DDEVICE_BOARD_NAME=W5500_EVB_PICO2)

add_definitions(-DPICO_DEFAULT_UART_TX_PIN=28)
add_definitions(-DPICO_DEFAULT_UART_RX_PIN=29)     

add_definitions(-Dsyslog_size=128*1024)  # Increase syslog buffer to 128KB


message(STATUS "WIZNET_CHIP = ${WIZNET_CHIP}")

if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH ${CMAKE_SOURCE_DIR}/WIZnet-PICO-C/libraries/pico-sdk)
    message(STATUS "PICO_SDK_PATH = ${PICO_SDK_PATH}")
endif()

# Pull in Raspberry Pi Pico SDK (must be before project)

include(WIZnet-PICO-C/pico_sdk_import.cmake)
include(WIZnet-PICO-C/wiznet_pico_c_sdk_version.cmake)

project(da_spi2i2s C CXX ASM )

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

if(NOT DEFINED WIZNET_DIR)
    set(WIZNET_DIR ${CMAKE_SOURCE_DIR}/WIZnet-PICO-C/libraries/ioLibrary_Driver)
    message(STATUS "WIZNET_DIR = ${WIZNET_DIR}")
endif()

if(NOT DEFINED MBEDTLS_DIR)
    set(MBEDTLS_DIR ${CMAKE_SOURCE_DIR}/WIZnet-PICO-C/libraries/mbedtls)
    message(STATUS "MBEDTLS_DIR = ${MBEDTLS_DIR}")
endif()

if(NOT DEFINED PORT_DIR)
    set(PORT_DIR ${CMAKE_SOURCE_DIR}/WIZnet-PICO-C/port)
    message(STATUS "PORT_DIR = ${PORT_DIR}")
endif()


set(ENABLE_PROGRAMS OFF CACHE BOOL "Build mbedtls programs")
set(ENABLE_TESTING OFF CACHE BOOL "Build mbedtls testing")
set(MBEDTLS_CONFIG_FILE "${PORT_DIR}/mbedtls/inc/ssl_config.h")
add_definitions(-DMBEDTLS_CONFIG_FILE="${PORT_DIR}/mbedtls/inc/ssl_config.h")
add_definitions(-DSET_TRUSTED_CERT_IN_SAMPLES)
add_definitions(-DPICO_USE_FASTEST_SUPPORTED_CLOCK=1)

# Set compile options
add_compile_options(
        -Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
        )

    
# Add libraries in subdirectories
add_subdirectory(${CMAKE_SOURCE_DIR}/WIZnet-PICO-C/libraries)
add_subdirectory(${MBEDTLS_DIR})
add_subdirectory(${PORT_DIR})


find_package(Git QUIET)

if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_COMMIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_TAG OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT GIT_TAG)
        set(GIT_TAG "")
    endif()
else()
    set(GIT_BRANCH "unknown")
    set(GIT_COMMIT_HASH "unknown")
    set(GIT_TAG "")
endif()

add_definitions(-DGIT_BRANCH="${GIT_BRANCH}")
add_definitions(-DGIT_HASH="${GIT_COMMIT_HASH}")
add_definitions(-DGIT_TAG="${GIT_TAG}")
message(STATUS "GIT_BRANCH: ${GIT_BRANCH}")
message(STATUS "GIT_HASH:   ${GIT_COMMIT_HASH}")
message(STATUS "GIT_TAG:    ${GIT_TAG}")


if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/daes67 OUTPUT_VARIABLE GIT_DAES67_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/daes67 OUTPUT_VARIABLE GIT_DAES67_HASH OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/daes67 OUTPUT_VARIABLE GIT_DAES67_TAG OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT GIT_DAES67_TAG)
        set(GIT_DAES67_TAG "")
    endif()
else()
    set(GIT_DAES67_BRANCH "unknown")
    set(GIT_DAES67_HASH "unknown")
    set(GIT_DAES67_TAG "")
endif()
add_definitions(-DGIT_DAES67_BRANCH="${GIT_DAES67_BRANCH}")
add_definitions(-DGIT_DAES67_HASH="${GIT_DAES67_HASH}")
add_definitions(-DGIT_DAES67_TAG="${GIT_DAES67_TAG}")
message(STATUS "GIT_DAES67_BRANCH: ${GIT_DAES67_BRANCH}")
message(STATUS "GIT_DAES67_HASH:   ${GIT_DAES67_HASH}")
message(STATUS "GIT_DAES67_TAG:    ${GIT_DAES67_TAG}")


add_executable(da_spi2i2s) 
target_compile_definitions(da_spi2i2s PRIVATE PFB_AES_KEY="${PFB_AES_KEY}")

pico_generate_pio_header(da_spi2i2s ${CMAKE_CURRENT_LIST_DIR}/i2s.pio)
pico_generate_pio_header(da_spi2i2s ${CMAKE_CURRENT_LIST_DIR}/spi.pio)

#Target_sources(da_spi2i2s  PRIVATE wizchip_http_server.c)
                            
Target_sources(da_spi2i2s   PRIVATE 
                            da_spi2i2s.cpp 
                            core_audio.cpp 
                            core_web.cpp 
                            w5500_udp.cpp
                            flash.cpp
                            daes67/src/histogram.cpp 
                            daes67/src/log.cpp 
                            daes67/src/stretch.cpp 
                            daes67/src/stretch_math.cpp
                            daes67/src/http_server.cpp
                            )

target_include_directories(da_spi2i2s PRIVATE ${CMAKE_CURRENT_LIST_DIR}/daes67/include)

pico_set_program_name(da_spi2i2s "da_spi2i2s")
pico_set_program_version(da_spi2i2s "1.0.1")

# no_flash means the target is to run from RAM
# pico_set_binary_type(da_spi2i2s no_flash)
pico_enable_stdio_uart(da_spi2i2s  1)
pico_enable_stdio_usb(da_spi2i2s 1)

# Add any user requested libraries
target_link_libraries(da_spi2i2s
        hardware_i2c
        hardware_dma
        hardware_pio
        hardware_clocks
        hardware_flash
        hardware_sync
        hardware_spi
        hardware_adc
        hardware_pwm
        pico_unique_id
        pico_multicore
        pico_stdlib
        IOLIBRARY_FILES
        ETHERNET_FILES
        DHCP_FILES
        )

pico_add_extra_outputs(da_spi2i2s)
