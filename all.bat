@echo off
echo ============================================
echo  STM32 Project Tools
echo ============================================
echo.
echo  1. Build firmware
echo  2. Build and flash
echo  3. Flash only
echo  4. Debug
echo  5. Clean
echo  6. Exit
echo.
set /p CHOICE="Select option (1-6): "

if "%CHOICE%"=="1" (
    call build.bat
) else if "%CHOICE%"=="2" (
    call build.bat
    if %errorlevel% equ 0 call flash.bat
) else if "%CHOICE%"=="3" (
    call flash.bat
) else if "%CHOICE%"=="4" (
    call debug.bat
) else if "%CHOICE%"=="5" (
    if exist build (
        echo Cleaning build directory...
        rmdir /s /q build
        echo Done.
    ) else (
        echo No build directory found.
    )
) else if "%CHOICE%"=="6" (
    exit /b 0
) else (
    echo Invalid choice!
)