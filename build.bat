@echo off
chcp 65001 >nul 2>&1
setlocal enabledelayedexpansion

REM ============================================
REM STM32 Firmware Build Script
REM ============================================



set "TOOLCHAIN_DIR=D:\arm-gnu-toolchain-14.3.rel1-mingw-w64-i686-arm-none-eabi\arm-gnu-toolchain-14.3.rel1-mingw-w64-i686-arm-none-eabi"
set "BUILD_DIR=%cd%\build"
set "PROJECT_DIR=%cd%"

echo.
echo ============================================
echo  STM32 Firmware Builder
echo ============================================
echo.

REM 检查工具链
if not exist "%TOOLCHAIN_DIR%\bin\arm-none-eabi-gcc.exe" (
    echo [ERROR] ARM GCC toolchain not found!

    echo Check path: %TOOLCHAIN_DIR%
    echo.
    echo Available directories:
    dir "D:\arm-gnu-toolchain-14.3.rel1-mingw-w64-i686-arm-none-eabi" 2>nul
    dir "D:\arm-gnu-toolchain*" 2>nul
    exit /b 1
)

REM 检查CMake
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] CMake not found in PATH!
    echo Please install CMake or add to PATH:
    echo   https://cmake.org/download/
    echo.
    echo Or set CMAKE_DIR variable in this script
    exit /b 1
)

REM 创建构建目录
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM 调用CMake构建
if exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [INFO] Incremental build...
    cd /d "%BUILD_DIR%"
    cmake --build .
) else (
    echo [INFO] Fresh build...
    cd /d "%BUILD_DIR%"

    cmake -DCMAKE_TOOLCHAIN_FILE="%PROJECT_DIR%\cmake\toolchain-arm-none-eabi.cmake" -DCMAKE_BUILD_TYPE=Debug ..
    if !errorlevel! neq 0 (
        echo [ERROR] CMake configure failed!
        exit /b 1
    )
    cmake --build .
)

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build failed!
    exit /b 1
)


cd /d "%PROJECT_DIR%"
echo.
echo [OK] Build complete!
echo.
echo Build output:
dir /s /b "%BUILD_DIR%\*.elf" "%BUILD_DIR%\*.hex" "%BUILD_DIR%\*.bin" 2>nul

endlocal
