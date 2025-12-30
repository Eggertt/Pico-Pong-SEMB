# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# Generated Cmake Pico project file
cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)
set(PICO_EXTRAS_FETCH_FROM_GIT ON)
include(pico_extras_import.cmake)
if(NOT DEFINED ENV{FREERTOS_KERNEL_PATH} AND NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../FreeRTOS-Kernel")
    include(FetchContent)
    FetchContent_Declare(
        FreeRTOS-Kernel
        GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
        GIT_TAG V10.5.1
    )
    FetchContent_GetProperties(FreeRTOS-Kernel)
    if(NOT freertos-kernel_POPULATED)
        FetchContent_Populate(FreeRTOS-Kernel)
    endif()
    set(FREERTOS_KERNEL_PATH ${freertos-kernel_SOURCE_DIR})
endif()

set(FREERTOS_CONFIG_FILE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
include(FreeRTOS_Kernel_import.cmake)

project(main C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. 
add_executable(main src/main.c src/game.c src/vga.c)

# Pico SDK Libraries
target_link_libraries( main
    pico_stdlib
    pico_multicore
    pico_scanvideo_dpi
)

# FreeRTOS Libraries
target_link_libraries( main
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap1
)

# Add the standard include files to the build
target_include_directories( main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_options( main PUBLIC
    ### Gnu/Clang C Options
    $<$<COMPILE_LANG_AND_ID:C,GNU>:-fdiagnostics-color=always>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-fcolor-diagnostics>

    $<$<COMPILE_LANG_AND_ID:C,Clang,GNU>:-Wall>
    $<$<COMPILE_LANG_AND_ID:C,Clang,GNU>:-Wextra>
    # $<$<COMPILE_LANG_AND_ID:C,Clang,GNU>:-Werror>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Weverything>
)

pico_set_program_name(main "pico_vga_arcade")
pico_set_program_version(main "0.1")

pico_enable_stdio_uart(main 0)
pico_enable_stdio_usb(main 1)

pico_add_extra_outputs(main)

