 cmake_minimum_required(VERSION 3.12)

set(PFB_AES_KEY "DickinsAudio2025Firmware" CACHE STRING "AES key for PFB")


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
message(STATUS "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")

set(BOARD_NAME W5500_EVB_PICO2)
set(PICO_PLATFORM rp2350)
set(WIZNET_CHIP W5500)
add_definitions(-D_WIZCHIP_=W5500)
add_definitions(-DDEVICE_BOARD_NAME=W5500_EVB_PICO2)
add_definitions(-DPICO_DEF)
add_definitions(-DPICO_DEFAULT_UART_TX_PIN=12)
add_definitions(-DPICO_DEFAULT_UART_RX_PIN=13)     

add_definitions(-Dsyslog_size=128*1024)  # Increase syslog buffer to 128KB


message(STATUS "WIZNET_CHIP = ${WIZNET_CHIP}")

if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH ${CMAKE_SOURCE_DIR}/WIZnet-PICO-C/libraries/pico-sdk)
    message(STATUS "PICO_SDK_PATH = ${PICO_SDK_PATH}")
endif()

# Pull in Raspberry Pi Pico SDK (must be before project)

include(WIZnet-PICO-C/pico_sdk_import.cmake)
include(WIZnet-PICO-C/wiznet_pico_c_sdk_version.cmake)

project(da_rpfir C CXX ASM )

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Ensure you're building the ARM M33 platform (not the RISC-V/Hazard3 one)
# (Your current "rp2350" may work, but this makes it explicit.)
set(PICO_PLATFORM rp2350-arm-s)  # :contentReference[oaicite:2]{index=2}

add_library(cmsis_dsp STATIC)
set(CMSIS_DSP_ROOT ${CMAKE_CURRENT_LIST_DIR}/extern/CMSIS-DSP)

file(GLOB_RECURSE CMSIS_DSP_SRC
  ${CMSIS_DSP_ROOT}/Source/*/*.c
)

# 1) Don't compile per-function sources (compiled via aggregators)
list(FILTER CMSIS_DSP_SRC EXCLUDE REGEX ".*/arm_.*\\.c$")

# 2) Don't compile internal fragments/templates (leading underscore)
list(FILTER CMSIS_DSP_SRC EXCLUDE REGEX ".*/_.*\\.c$")

# 3) Optional: disable float16 (recommended unless you explicitly need it)
list(FILTER CMSIS_DSP_SRC EXCLUDE REGEX ".*/.*F16.*\\.c$")
target_compile_definitions(cmsis_dsp PUBLIC DISABLEFLOAT16)

target_sources(cmsis_dsp PRIVATE ${CMSIS_DSP_SRC})

target_include_directories(cmsis_dsp PUBLIC
  ${CMSIS_DSP_ROOT}/Include
  ${CMSIS_DSP_ROOT}/PrivateInclude
)

# Inherit Pico SDK CMSIS-Core paths/glue
target_link_libraries(cmsis_dsp PUBLIC cmsis_core)

target_compile_definitions(cmsis_dsp PUBLIC
  ARM_MATH_CM33
  ARM_MATH_LOOPUNROLL
)

# Optional: if you want CMSIS-DSP hotter than the rest of your code
target_compile_options(cmsis_dsp PRIVATE -O3)

# Enable garbage collection of unused sections (critical for removing unused FFT tables)
target_compile_options(cmsis_dsp PRIVATE -ffunction-sections -fdata-sections)
target_link_options(cmsis_dsp PUBLIC -Wl,--gc-sections)

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
        -ffunction-sections  # Put each function in its own section for garbage collection
        -fdata-sections      # Put each data item in its own section for garbage collection
        )

# Enable linker garbage collection to remove unused code/data
add_link_options(-Wl,--gc-sections)

    
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



add_executable(da_rpfir) 
target_compile_definitions(da_rpfir PRIVATE PFB_AES_KEY="${PFB_AES_KEY}")

pico_generate_pio_header(da_rpfir ${CMAKE_CURRENT_LIST_DIR}/i2s.pio)

#Target_sources(da_rpfir  PRIVATE wizchip_http_server.c)

                            
Target_sources(da_rpfir   PRIVATE 
                            da_rpfir.cpp 
                            core_audio.cpp 
                            core_web.cpp 
                            w5500_udp.cpp
                            flash.cpp
                            daes67_lite/histogram.cpp 
                            daes67_lite/log.cpp 
                            daes67_lite/http_server.cpp
                            pwm.c
                            )

target_include_directories(da_rpfir PRIVATE ${CMAKE_CURRENT_LIST_DIR}/daes67_lite)


pico_set_program_name(da_rpfir "da_rpfir")
pico_set_program_version(da_rpfir "1.0.1")

# no_flash means the target is to run from RAM
# pico_set_binary_type(da_rpfir no_flash)
pico_enable_stdio_uart(da_rpfir  1)
pico_enable_stdio_usb(da_rpfir 1)

# Add any user requested libraries
target_link_libraries(da_rpfir cmsis_dsp
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

pico_add_extra_outputs(da_rpfir)

# Add custom target to display RAM usage - runs every time make is invoked
add_custom_target(ram_usage ALL
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "════════════════════════════════════════════════════════════"
    COMMAND ${CMAKE_COMMAND} -E echo "RP2350 RAM USAGE ANALYSIS"
    COMMAND ${CMAKE_COMMAND} -E echo "════════════════════════════════════════════════════════════"
    COMMAND awk -f ${CMAKE_SOURCE_DIR}/scripts/ram_usage.awk ${CMAKE_BINARY_DIR}/da_rpfir.elf.map || echo "Map file not found - run 'make da_rpfir' first"
    COMMAND ${CMAKE_COMMAND} -E echo "════════════════════════════════════════════════════════════"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    DEPENDS da_rpfir
    COMMENT "Analyzing RAM usage..."
)
