:: =============================================
::  CustomPE - Custom Windows PE for OEM Deployment
::  Author: AlexMartin
::  Last updated: 2026-08-07
:: =============================================
@echo off
setlocal EnableDelayedExpansion

:: Load global configuration (Build-WinPE.conf)
if exist "%~dp0Build-WinPE.conf" (
    for /f "usebackq eol=# tokens=1,* delims==" %%a in ("%~dp0Build-WinPE.conf") do set "%%a=%%b"
)
if not defined CustomPE_NAME set "CustomPE_NAME=CustomPE"
if not defined CustomPE_AUTHOR set "CustomPE_AUTHOR=AlexMartin"
if not defined CustomPE_VERSION set "CustomPE_VERSION=1.0"
if not defined CustomPE_WORKDIR set "CustomPE_WORKDIR=E:\WinPE-Build"
if not defined CustomPE_DRVREPO set "CustomPE_DRVREPO=E:\DriversRepo"

:: Build log folder follows CustomPE_NAME, with the legacy "CustomPE" fallback
:: (same resolution as Build-WinPE.ps1)
set "LOG_FOLDER=%CustomPE_NAME%"
if not exist "%~dp0%CustomPE_NAME%" (
    if exist "%~dp0CustomPE" ( set "LOG_FOLDER=CustomPE" ) else ( set "LOG_FOLDER=" )
)

title %CustomPE_NAME% Builder v%CustomPE_VERSION%

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Administrator privileges required.
    pause
    exit /b 1
)

if not exist "%~dp0Build-WinPE.ps1" (
    echo [ERROR] Build-WinPE.ps1 not found next to this script.
    pause
    exit /b 1
)

echo ============================================
echo  %CustomPE_NAME% Builder v%CustomPE_VERSION%
echo  Output: %CustomPE_WORKDIR%\ISO\%CustomPE_NAME%.iso
echo  Drivers: %CustomPE_DRVREPO%
echo ============================================
echo.

:: Delegate the entire build to Build-WinPE.ps1 (single implementation)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-WinPE.ps1" -WorkDir "%CustomPE_WORKDIR%" -DriverRoot "%CustomPE_DRVREPO%"
set "PS_EXIT=%errorlevel%"

if not "%PS_EXIT%"=="0" (
    echo.
    if defined LOG_FOLDER (
        echo [ERROR] Build failed ^(exit code %PS_EXIT%^). See %LOG_FOLDER%\Logs\Build.log for details.
    ) else (
        echo [ERROR] Build failed ^(exit code %PS_EXIT%^). See Logs\Build.log in the project folder for details.
    )
    pause
    exit /b %PS_EXIT%
)

echo.
echo  Build completed. Press any key to close.
pause >nul
endlocal
