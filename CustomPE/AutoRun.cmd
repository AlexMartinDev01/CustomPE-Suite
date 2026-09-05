:: =============================================
::  CustomPE - Custom Windows PE for OEM Deployment
::  Author: AlexMartin
::  Last updated: 2026-08-07
:: =============================================
@echo off
setlocal EnableDelayedExpansion

:: Load global configuration (Build-WinPE.conf on ISO/USB root)
if exist "%~dp0Build-WinPE.conf" (
    for /f "usebackq eol=# tokens=1,* delims==" %%a in ("%~dp0Build-WinPE.conf") do set "%%a=%%b"
)
if not defined CustomPE_NAME set "CustomPE_NAME=CustomPE"
if not defined CustomPE_AUTHOR set "CustomPE_AUTHOR=AlexMartin"
if not defined CustomPE_VERSION set "CustomPE_VERSION=1.0"

:: =====================================================
::   CustomPE AutoRun.cmd - External Deployment Interface
::   Location: ISO/USB Root (modify freely, no WIM rebuild)
::   Called by: startnet.cmd inside boot.wim
:: =====================================================

:: ---- Logging: real-time console + file into <CustomPE_NAME>\Logs ----
set "LOG_DIR=%~dp0%CustomPE_NAME%\Logs"
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%" >nul 2>&1
set "LOG_PROBE=%LOG_DIR%\.probe"
echo x > "%LOG_PROBE%" 2>nul
if not exist "%LOG_PROBE%" (
    set "LOG_DIR=X:\!CustomPE_NAME!\Logs"
    if not exist "!LOG_DIR!" mkdir "!LOG_DIR!" >nul 2>&1
)
del /f /q "%LOG_PROBE%" >nul 2>&1
set "LOG_FILE=%LOG_DIR%\AutoRun.log"
if exist "%LOG_FILE%" del /f /q "%LOG_FILE%" >nul 2>&1
echo ============================================ >  "%LOG_FILE%" 2>nul
echo  %CustomPE_NAME% - AutoRun Log                  >> "%LOG_FILE%" 2>nul
echo  Started: %date% %time%                         >> "%LOG_FILE%" 2>nul
echo  Log: %LOG_FILE%                                >> "%LOG_FILE%" 2>nul
echo ============================================ >> "%LOG_FILE%" 2>nul
echo. >> "%LOG_FILE%" 2>nul

call :Log "============================================"
call :Log "  %CustomPE_NAME% External Deployment Script"
call :Log "  Author: %CustomPE_AUTHOR%"
call :Log "  Last updated: %~t0"
call :Log "============================================"

:: -- Detect source drive (where this script resides)
set SRC=%~d0
call :Log "[%date% %time%] Source drive: %SRC%"

call :Log "[%date% %time%] AutoRun.cmd started."

:: ============================================
::  USER EDIT HERE - Add deployment logic below
:: ============================================
::
::  Available variables:
::    %SRC%       = Drive containing AutoRun.cmd
::    %CustomPE_NAME%\  = Tools and scripts directory
::
::  Quick test:
::    call "%SRC%\%CustomPE_NAME%\Scripts\Test_Components.cmd"
:: ============================================

:: -- OEM deployment UI (CustomPE\Tools\DeployUI): minimize this console, then
::    start WinPE.exe WITHOUT /wait so the CMD prompt stays usable at the same
::    time (UI and console do not block each other). Working directory is the
::    UI folder so the bundled OEM_Partition_Schemes.xml is found automatically.
set "UI_DIR=%SRC%\%CustomPE_NAME%\Tools\DeployUI"
if exist "%UI_DIR%\WinPE.exe" (
    call :Log "[%date% %time%] Minimizing console..."
    if exist "%UI_DIR%\MinimizeWindow.exe" "%UI_DIR%\MinimizeWindow.exe"
    call :Log "[%date% %time%] Starting WinPE UI (independent, CMD stays available): %UI_DIR%\WinPE.exe"
    pushd "%UI_DIR%"
    start "" "WinPE.exe"
    popd
    call :Log "[%date% %time%] WinPE UI launched; CMD prompt remains available."
) else (
    call :Log "[WARN] DeployUI not found, skipping UI launch: %UI_DIR%"
)

rem call "%SRC%\%CustomPE_NAME%\Scripts\Test_Components.cmd"

call :Log "[%date% %time%] AutoRun.cmd completed."
call :Log ""
call :Log "  Log file: %LOG_FILE%"
echo.
echo ============================================
echo   Log file: %LOG_FILE%
echo ============================================
echo.
endlocal

:: -- Put the command starting point in this script own folder
::    (works no matter which drive/folder this script is copied to)
cd /d "%~dp0"
cmd /k
goto :EOF

:Log
echo(%~1
if defined LOG_FILE echo(%~1 >> "%LOG_FILE%" 2>nul
exit /b 0
