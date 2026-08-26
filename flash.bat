@echo off
REM ============================================
REM STM32 Firmware Flash Script
REM ============================================

set OPENOCD_DIR=D:\openocd-20260302\OpenOCD-20260302-0.12.0
set BUILD_DIR=%cd%\build
set ELF_FILE=%BUILD_DIR%\fw-debug\washer_hil_fw.elf

echo.
echo ============================================
echo  STM32 Firmware Programmer
echo ============================================
echo.

REM 检查ELF文件
if not exist "%ELF_FILE%" (
    echo [ERROR] Firmware file not found: %ELF_FILE%
    echo Please run build.bat first
    exit /b 1
)

REM 选择烧录模式
echo Select flash mode:
echo   1. Flash and Run
echo   2. Flash only (halt)
echo   3. Erase chip
echo   4. Flash and Reset
set /p MODE="Enter choice (1-4): "

set OPENOCD_CMD="%OPENOCD_DIR%\bin\openocd.exe" -f interface/stlink-hla.cfg -f target/stm32f1x.cfg -c "adapter speed 500" -c "init"

if "%MODE%"=="1" (
    echo.
    echo [INFO] Flashing and running...
    %OPENOCD_CMD% -c "program %ELF_FILE% verify reset exit"
) else if "%MODE%"=="2" (
    echo.
    echo [INFO] Flashing (halt at entry)...
    %OPENOCD_CMD% -c "program %ELF_FILE% verify exit"
) else if "%MODE%"=="3" (
    echo.
    echo [WARNING] Erasing entire chip!
    set /p CONFIRM="Are you sure? (y/n): "
    if /i "%CONFIRM%"=="y" (
        %OPENOCD_CMD% -c "stm32f1x mass_erase 0" -c "exit"
    ) else (
        echo Canceled.
        exit /b 0
    )
) else if "%MODE%"=="4" (
    echo.
    echo [INFO] Flashing and resetting...
    %OPENOCD_CMD% -c "halt" -c "stm32f1x lock 0" -c "program %ELF_FILE% verify reset exit"
) else (
    echo [ERROR] Invalid choice!
    exit /b 1
)

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Flash failed!
    exit /b 1
)

echo.
echo [OK] Flash operation complete!