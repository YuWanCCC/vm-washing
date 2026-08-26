#
# 洗衣机 HIL 仿真板 STM32F103ZET6 固件 - 交叉编译配置
#
# 用法:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
# 产物: build/washer_hil_fw.elf / .hex / .bin
#
# 推荐直接用预设(工具链路径已写在 firmware/CMakePresets.json):
#   cmake --preset fw-release
#   cmake --build --preset fw-release -j
#
# 需要 arm-none-eabi 工具链在 PATH 中, 或通过
#   -DTOOLCHAIN_PREFIX=/path/to/bin/arm-none-eabi-
# 指定。
#

cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# 工具链设置(必须在 project() 之前)
# ---------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT DEFINED TOOLCHAIN_PREFIX)
    set(TOOLCHAIN_PREFIX "arm-none-eabi-")
endif()

# TOOLCHAIN_PREFIX 为绝对路径时, Windows 下必须补 .exe,
# 否则 project() 会判定编译器不存在; 裸前缀则交给 PATH 查找。
if(CMAKE_HOST_WIN32 AND IS_ABSOLUTE "${TOOLCHAIN_PREFIX}")
    set(TOOLCHAIN_SUFFIX ".exe")
else()
    set(TOOLCHAIN_SUFFIX "")
endif()

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc${TOOLCHAIN_SUFFIX})
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc${TOOLCHAIN_SUFFIX})
set(CMAKE_OBJCOPY      ${TOOLCHAIN_PREFIX}objcopy${TOOLCHAIN_SUFFIX})
set(CMAKE_SIZE         ${TOOLCHAIN_PREFIX}size${TOOLCHAIN_SUFFIX})

# 交叉编译时不能运行测试程序, 编译静态库来做编译器检测
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(washer_hil_fw C ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# 目标 MCU 与编译选项
# ---------------------------------------------------------------------------
set(MCU_FLAGS
    -mcpu=cortex-m3
    -mthumb
    -mfloat-abi=soft
)

add_compile_options(
    ${MCU_FLAGS}
    -Wall
    -Wextra
    -Wshadow
    -Wundef
    -fdata-sections          # 配合 --gc-sections 去除未使用代码
    -ffunction-sections
    -fno-common
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Debug>:-gdwarf-2>
)

# STM32F103ZET6: Cortex-M3, 512K FLASH, 64K RAM(大容量/High-density 器件)
add_compile_definitions(
    STM32F103xE
    STM32F10X_HD
    $<$<CONFIG:Debug>:DEBUG>
)

# ---------------------------------------------------------------------------
# 源文件
# ---------------------------------------------------------------------------
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/STM32F103ZETx_FLASH.ld)

set(SOURCES
    Core/Src/main.c
    Core/Src/uart_handler.c
    Core/Src/protocol_parser.c
    Core/Src/dac_output.c
    Core/Src/pwm_output.c
    Core/Src/gpio_driver.c
    Core/Src/adc_sampler.c
    Core/Src/crc16.c
    Core/Src/system_stm32f1xx.c
    Core/Src/stm32f1xx_it.c
    startup/startup_stm32f103zetx.s
)

add_executable(${PROJECT_NAME}.elf ${SOURCES})

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    Core/Inc
)

# ---------------------------------------------------------------------------
# 链接选项
# ---------------------------------------------------------------------------
target_link_options(${PROJECT_NAME}.elf PRIVATE
    ${MCU_FLAGS}
    -T${LINKER_SCRIPT}
    --specs=nano.specs      # 使用精简 C 库
    --specs=nosys.specs     # 无操作系统, 系统调用桩
    -nostartfiles           # 启动代码由 startup_stm32f103zetx.s 提供
    -Wl,--gc-sections
    -Wl,-Map=${PROJECT_NAME}.map,--cref
    -Wl,--print-memory-usage
)

# 链接脚本变化时重新链接
set_target_properties(${PROJECT_NAME}.elf PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

# ---------------------------------------------------------------------------
# 生成 hex/bin 并打印尺寸
# ---------------------------------------------------------------------------
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
    COMMENT "生成 hex/bin 并统计固件尺寸"
)

# ---------------------------------------------------------------------------
# 便捷烧写目标: cmake --build <dir> --target flash
# 走 OpenOCD + ST-Link(SWD), 直接烧 elf。
# OPENOCD 默认靠 PATH 查找, 实际路径写在 CMakePresets.json 的 fw-base 中。
# ---------------------------------------------------------------------------
set(OPENOCD "openocd" CACHE STRING "OpenOCD 可执行文件(裸名称或完整路径)")

add_custom_target(flash
    COMMAND ${OPENOCD}
            -f interface/stlink.cfg
            -f target/stm32f1x.cfg
            -c "program $<TARGET_FILE:${PROJECT_NAME}.elf> verify reset exit"
    DEPENDS ${PROJECT_NAME}.elf
    COMMENT "通过 OpenOCD + ST-Link(SWD) 烧写固件"
)
