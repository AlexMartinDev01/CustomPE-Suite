:: =============================================
::  CustomPE - Custom Windows PE for OEM Deployment
::  Author: AlexMartin
::  Last updated: 2026-08-07
:: =============================================
@echo off
setlocal EnableDelayedExpansion

:: Defaults only - the media config (Build-WinPE.conf) is loaded inside the
:: drive scan below, because startnet.cmd itself runs from X: inside boot.wim
:: and cannot see the media drive until the media is located.
if not defined CustomPE_NAME set "CustomPE_NAME=CustomPE"
if not defined CustomPE_AUTHOR set "CustomPE_AUTHOR=AlexMartin"
if not defined CustomPE_VERSION set "CustomPE_VERSION=1.0"

:: ---- Logging: buffer to X: (RAM disk), flushed to <CustomPE_NAME>\Logs on the
::      media drive once found (folder name follows the loaded media config) ----
set "LOG_DIR=X:\CustomPE\Logs"
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%" >nul 2>&1
set "LOG_FILE=%LOG_DIR%\Startnet.log"
if exist "%LOG_FILE%" del /f /q "%LOG_FILE%" >nul 2>&1
echo ============================================ >  "%LOG_FILE%" 2>nul
echo  %CustomPE_NAME% - Startnet Log                >> "%LOG_FILE%" 2>nul
echo  Started: %date% %time%                        >> "%LOG_FILE%" 2>nul
echo ============================================ >> "%LOG_FILE%" 2>nul
echo. >> "%LOG_FILE%" 2>nul

call :Log "============================================"
call :Log "  %CustomPE_NAME% Preinstallation Environment"
call :Log "  Author: %CustomPE_AUTHOR%"
call :Log "  Last updated: %~t0"
call :Log "  Built: 2026-08-07"
call :Log "============================================"
call :Log "[%date% %time%] Initializing WinPE..."
:: wpeinit output goes to the RAM-disk log so the console does not scroll during initialization
wpeinit > "%LOG_DIR%\wpeinit.log" 2>&1
if %errorlevel% neq 0 call :Log "[WARN] wpeinit reported errors - see %LOG_DIR%\wpeinit.log"
call :Log "[%date% %time%] wpeinit completed."
call :Log ""

call :Log "[%date% %time%] Scanning for AutoRun.cmd on all drives..."
:: -- AutoRun.cmd (external interface) sets its own working directory and
::    provides the interactive command prompt, so do not continue here.
::    A drive is only accepted when it carries BOTH AutoRun.cmd and
::    Build-WinPE.conf, so a stale copy on a hard disk cannot be picked up.
for %%d in (C D E F G H I J K L M N O P Q R S T U V W X Y Z) do (
    if exist "%%d:\AutoRun.cmd" if exist "%%d:\Build-WinPE.conf" (
        :: load the media configuration from the AutoRun drive
        for /f "usebackq eol=# tokens=1,* delims==" %%a in ("%%d:\Build-WinPE.conf") do set "%%a=%%b"
        call :Log "[%date% %time%] Found: %%d:\AutoRun.cmd"
        call :Log "[%date% %time%] Loaded config: %%d:\Build-WinPE.conf"
        :: first entry on the media: auto-clear Logs, then flush the bootstrap log
        set "MEDIA_LOG_DIR=%%d:\!CustomPE_NAME!\Logs"
        if not exist "!MEDIA_LOG_DIR!" mkdir "!MEDIA_LOG_DIR!" >nul 2>&1
        set "MEDIA_PROBE=!MEDIA_LOG_DIR!\.probe"
        echo x > "!MEDIA_PROBE!" 2>nul
        if exist "!MEDIA_PROBE!" (
            del /f /q "!MEDIA_PROBE!" >nul 2>&1
            del /f /q "!MEDIA_LOG_DIR!\*.log" >nul 2>&1
            copy /y "!LOG_FILE!" "!MEDIA_LOG_DIR!\Startnet.log" >nul 2>&1
            copy /y "!LOG_DIR!\wpeinit.log" "!MEDIA_LOG_DIR!\wpeinit.log" >nul 2>&1
            set "LOG_FILE=!MEDIA_LOG_DIR!\Startnet.log"
        ) else (
            del /f /q "!MEDIA_PROBE!" >nul 2>&1
        )
        call :Log "[%date% %time%] Log file: !LOG_FILE!"
        call :Log "[%date% %time%] Executing: %%d:\AutoRun.cmd"
        call "%%d:\AutoRun.cmd"
        exit /b
    )
)

:: No AutoRun.cmd found
call :Log ""
call :Log "[ERROR] AutoRun.cmd / Build-WinPE.conf not found on any drive!"
call :Log "Please place AutoRun.cmd and Build-WinPE.conf in the root directory of a USB/ISO drive."
call :Log ""
cmd /k
goto :EOF

:Log
echo(%~1
if defined LOG_FILE echo(%~1 >> "%LOG_FILE%" 2>nul
exit /b 0
