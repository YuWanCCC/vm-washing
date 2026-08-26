@echo off
REM ============================================
REM STM32 Firmware Debug Script
REM ============================================

set OPENOCD_DIR=D:\openocd-20260302\OpenOCD-20260302-0.12.0
set BUILD_DIR=%cd%\build
set ELF_FILE=%BUILD_DIR%\fw-debug\washer_hil_fw.elf

echo.
echo ============================================
echo  STM32 Firmware Debugger
echo ============================================
echo.

REM 测试GDB
set GDB_EXE=D:\arm-gnu-toolchain-14.3.rel1-mingw-w64-i686-arm-none-eabi\arm-gnu-toolchain-14.3.rel1-mingw-w64-i686-arm-none-eabi\bin\arm-none-eabi-gdb.exe
if not exist "%GDB_EXE%" (
    echo [ERROR] GDB not found: %GDB_EXE%
    exit /b 1
)

echo [1] Start OpenOCD server
echo [2] Start VSCode debug
echo [3] Command line GDB
echo [4] Flash and debug
set /p MODE="Enter choice (1-4): "

if "%MODE%"=="1" (
    echo Starting OpenOCD server...
    %OPENOCD_DIR%\bin\openocd.exe -f interface/stlink-hla.cfg -f target/stm32f1x.cfg -c "adapter speed 500" -c "init" -c "targets"
) else if "%MODE%"=="2" (
    echo Starting VSCode...
    start code .
) else if "%MODE%"=="3" (
    echo Starting GDB...
    %GDB_EXE% -ex "target remote localhost:3333" -ex "monitor reset halt" -ex "load" "%ELF_FILE%"
) else if "%MODE%"=="4" (
    echo Flashing and starting GDB...
    start "OpenOCD" %OPENOCD_DIR%\bin\openocd.exe -f interface/stlink-hla.cfg -f target/stm32f1x.cfg -c "adapter speed 500" -c "program %ELF_FILE% verify reset halt"
    timeout /t 3
    %GDB_EXE% -ex "target remote localhost:3333" "%ELF_FILE%"
) else (
    echo [ERROR] Invalid choice!
    exit /b 1
)