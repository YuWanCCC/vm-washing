cmake_minimum_required(VERSION 3.16)

project(WashingMachineHIL
    VERSION 0.1.0
    DESCRIPTION "Washing machine HIL simulation platform"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 测试目标定义在 pc-app 中，此处启用 CTest 以便从构建根目录直接运行 ctest
enable_testing()

# PC 端上位机（Qt Quick 仿真与监控界面）
add_subdirectory(pc-app)

# 下位机固件（STM32 信号发生/采集），由后续任务补充，届时取消注释
# add_subdirectory(firmware)
