cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(MerlinCCU C CXX ASM)

pico_sdk_init()

add_executable(MerlinCCU MerlinCCU.cpp)

pico_set_program_name(MerlinCCU "MerlinCCU")
pico_set_program_version(MerlinCCU "0.1")

pico_generate_pio_header(MerlinCCU ${CMAKE_CURRENT_LIST_DIR}/el320_raster.pio)

pico_enable_stdio_uart(MerlinCCU 0)
pico_enable_stdio_usb(MerlinCCU 1)

target_link_libraries(MerlinCCU
    pico_stdlib
    hardware_pio
    hardware_dma
)

target_include_directories(MerlinCCU PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(MerlinCCU)