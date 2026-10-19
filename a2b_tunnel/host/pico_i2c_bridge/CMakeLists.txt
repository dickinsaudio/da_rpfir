cmake_minimum_required(VERSION 3.13)

# Standalone USB-CDC -> I2C master bridge for driving an AD242x A2B main node.
# Configure with e.g.:
#   cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_BOARD=pico   (or pico2)
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico CACHE STRING "Board type")
endif()

include(pico_sdk_import.cmake)

project(pico_i2c_bridge C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(pico_i2c_bridge main.c)

# Pin / instance overrides, e.g. -DBRIDGE_SDA_PIN=0 -DBRIDGE_SCL_PIN=1
foreach(opt BRIDGE_I2C_INDEX BRIDGE_SDA_PIN BRIDGE_SCL_PIN BRIDGE_DEFAULT_KHZ)
    if(DEFINED ${opt})
        target_compile_definitions(pico_i2c_bridge PRIVATE ${opt}=${${opt}})
    endif()
endforeach()

target_link_libraries(pico_i2c_bridge pico_stdlib hardware_i2c)

pico_enable_stdio_usb(pico_i2c_bridge 1)
pico_enable_stdio_uart(pico_i2c_bridge 0)

pico_add_extra_outputs(pico_i2c_bridge)
