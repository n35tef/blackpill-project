cmake_minimum_required(VERSION 3.22)
enable_language(C ASM)

# ---------------------------------------------------------------------------
# BlackPill (STM32F411CEU6) MCU/board support layer.
#
# This is the reusable base: CMSIS device headers, the trimmed STM32F4xx HAL
# driver, the startup file / linker script, and the Bsp/ peripheral modules.
# To use this base for a new project you should not need to touch this file
# - only Bsp/Inc/bsp_config.h to toggle peripherals, and Core/Src/main.c (or
# your own app sources) for application logic.
# ---------------------------------------------------------------------------

set(MCU_DEFINES
    USE_HAL_DRIVER
    STM32F411xE
    $<$<CONFIG:Debug>:DEBUG>
)

set(MCU_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/Bsp/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
)

# Application-facing sources: startup/interrupts + every Bsp/Src module.
# Each bsp_xxx.c is guarded by its own BSP_USE_xxx flag in bsp_config.h, so
# it is always safe to compile them all in - disabled peripherals just
# produce an (essentially) empty translation unit.
file(GLOB BSP_SOURCES ${CMAKE_SOURCE_DIR}/Bsp/Src/*.c)

set(MCU_APPLICATION_SRC
    ${CMAKE_SOURCE_DIR}/Core/Src/stm32f4xx_it.c
    ${CMAKE_SOURCE_DIR}/Core/Src/sysmem.c
    ${CMAKE_SOURCE_DIR}/Core/Src/syscalls.c
    ${CMAKE_SOURCE_DIR}/startup_stm32f411xe.s
    ${BSP_SOURCES}
)

# STM32 CMSIS + HAL driver sources
set(MCU_DRIVER_SRC
    ${CMAKE_SOURCE_DIR}/Core/Src/system_stm32f4xx.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c
)

# Interface library: shared include paths + preprocessor defines
add_library(blackpill-mcu-interface INTERFACE)
target_include_directories(blackpill-mcu-interface INTERFACE ${MCU_INCLUDE_DIRS})
target_compile_definitions(blackpill-mcu-interface INTERFACE ${MCU_DEFINES})

# Single static library: CMSIS/HAL driver + startup/interrupts + BSP.
# (Kept as one target rather than chaining OBJECT libraries through
# INTERFACE libraries, since CMake does not reliably propagate OBJECT
# library object files across more than one level of target_link_libraries.)
add_library(blackpill-mcu STATIC)
target_sources(blackpill-mcu PRIVATE ${MCU_APPLICATION_SRC} ${MCU_DRIVER_SRC})
target_link_libraries(blackpill-mcu PUBLIC blackpill-mcu-interface)

# NOTE: linker script / --specs=nano.specs / map file flags are already set
# globally by cmake/gcc-arm-none-eabi.cmake (CMAKE_EXE_LINKER_FLAGS), so they
# are not repeated here.

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    ADDITIONAL_CLEAN_FILES ${CMAKE_PROJECT_NAME}.map
)

if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(FATAL_ERROR "This BSP requires C11 or higher")
endif()
