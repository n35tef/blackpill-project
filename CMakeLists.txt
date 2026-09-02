cmake_minimum_required(VERSION 3.22)

# C11 is required: the register map verifies itself with _Static_assert.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME blackpill-project)

# Emit compile_commands.json so clangd works out of the box.
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME} LANGUAGES C)
message("Build type: " ${CMAKE_BUILD_TYPE})

add_executable(${CMAKE_PROJECT_NAME})

# Board support: CPU core, STM32F411 register map, startup, linker script and
# the peripheral modules toggled in Bsp/Inc/bsp_config.h.
add_subdirectory(cmake/mcu)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/main.c
)

target_link_libraries(${CMAKE_PROJECT_NAME}
    blackpill-mcu
)

# Re-link whenever the linker script changes.
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    LINK_DEPENDS ${CMAKE_SOURCE_DIR}/linker/stm32f411ce.ld
)

# ---------------------------------------------------------------------------
# Flashable artefacts and size report
# ---------------------------------------------------------------------------
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating ${CMAKE_PROJECT_NAME}.hex and ${CMAKE_PROJECT_NAME}.bin"
)

# ---------------------------------------------------------------------------
# Flash / debug helpers (all open source, no vendor tooling required)
# ---------------------------------------------------------------------------
find_program(OPENOCD_EXECUTABLE openocd)
find_program(STFLASH_EXECUTABLE st-flash)
find_program(PROBE_RS_EXECUTABLE probe-rs)

if(OPENOCD_EXECUTABLE)
    add_custom_target(flash
        COMMAND ${OPENOCD_EXECUTABLE}
                -f interface/stlink.cfg -f target/stm32f4x.cfg
                -c "program $<TARGET_FILE:${CMAKE_PROJECT_NAME}> verify reset exit"
        DEPENDS ${CMAKE_PROJECT_NAME}
        USES_TERMINAL
        COMMENT "Flashing with OpenOCD"
    )

    add_custom_target(gdb-server
        COMMAND ${OPENOCD_EXECUTABLE} -f interface/stlink.cfg -f target/stm32f4x.cfg
        USES_TERMINAL
        COMMENT "OpenOCD GDB server on :3333"
    )
elseif(STFLASH_EXECUTABLE)
    add_custom_target(flash
        COMMAND ${STFLASH_EXECUTABLE} --reset write ${CMAKE_PROJECT_NAME}.bin 0x08000000
        DEPENDS ${CMAKE_PROJECT_NAME}
        USES_TERMINAL
        COMMENT "Flashing with st-flash"
    )
elseif(PROBE_RS_EXECUTABLE)
    add_custom_target(flash
        COMMAND ${PROBE_RS_EXECUTABLE} download --chip STM32F411CEUx $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        DEPENDS ${CMAKE_PROJECT_NAME}
        USES_TERMINAL
        COMMENT "Flashing with probe-rs"
    )
endif()
