cmake_minimum_required(VERSION 3.22)
enable_language(C)

# ---------------------------------------------------------------------------
# BlackPill (STM32F411CEU6) MCU/board support layer.
#
# There is no vendor code in this project: the CPU core definitions, the
# STM32F411 register map, the startup file and the linker script are all in
# Bsp/. To use this base for a new project you should not need to touch this
# file - only Bsp/Inc/bsp_config.h to toggle peripherals, and Core/Src/main.c
# for application logic.
# ---------------------------------------------------------------------------

set(MCU_DEFINES
    STM32F411xE
    $<$<CONFIG:Debug>:DEBUG>
)

set(MCU_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/Bsp/Inc
)

# Every Bsp/Src module, including the device layer. Each bsp_xxx.c is guarded
# by its own BSP_USE_xxx flag in bsp_config.h, so it is always safe to compile
# them all in - disabled peripherals produce an empty translation unit.
file(GLOB BSP_SOURCES
    ${CMAKE_SOURCE_DIR}/Bsp/Src/*.c
    ${CMAKE_SOURCE_DIR}/Bsp/Src/device/*.c
)

add_library(blackpill-mcu-interface INTERFACE)
target_include_directories(blackpill-mcu-interface INTERFACE ${MCU_INCLUDE_DIRS})
target_compile_definitions(blackpill-mcu-interface INTERFACE ${MCU_DEFINES})

# Single static library rather than chained OBJECT libraries: CMake does not
# reliably propagate OBJECT library object files across more than one level of
# target_link_libraries.
add_library(blackpill-mcu STATIC)
target_sources(blackpill-mcu PRIVATE ${BSP_SOURCES})
target_link_libraries(blackpill-mcu PUBLIC blackpill-mcu-interface)

# NOTE: linker script / --specs=nano.specs / map file flags are already set
# globally by cmake/gcc-arm-none-eabi.cmake (CMAKE_EXE_LINKER_FLAGS), so they
# are not repeated here.

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    ADDITIONAL_CLEAN_FILES ${CMAKE_PROJECT_NAME}.map
)

if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(FATAL_ERROR "This BSP requires C11 or higher (_Static_assert)")
endif()
