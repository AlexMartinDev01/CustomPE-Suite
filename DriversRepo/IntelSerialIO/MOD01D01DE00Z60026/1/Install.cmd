
@echo off
REM Created by MVP v2.1.0.5
REM Please replace DriverProvider, DriverCategory, DriverItemType variables with exact names
title Installing Intel Intel Serial I/O Serial I/O
SET OSDST=%~d0
if not exist %~d0\OEM\Preload\Command\POP*.ini set OSDST=C:
if not exist %OSDST%\OEM\AcerLogs md %OSDST%\OEM\AcerLogs
SET LogPath=%OSDST%\OEM\AcerLogs\DriverInstallation.log
ECHO.>>%LogPath%
ECHO Installing, please wait...
SETLOCAL ENABLEDELAYEDEXPANSION
pushd "%~dp0"

ECHO %DATE% %TIME%[Log START]  ============ %~dpnx0 ============ >> %LogPath%


SET OSEnv=Online
ECHO %DATE% %TIME%[Log TRACE]  reg query "HKEY_LOCAL_MACHINE\Software\Microsoft\Windows NT\CurrentVersion\WinPE" >>%LogPath% 2>&1
reg query "HKEY_LOCAL_MACHINE\Software\Microsoft\Windows NT\CurrentVersion\WinPE" >>%LogPath% 2>&1
ECHO.>>%LogPath%
if !errorlevel! equ 0 (
	ECHO !DATE! !TIME![Log TRACE]  Running in the WinPE, SET OSEnv=Offline >>%LogPath%
	SET OSEnv=Offline
) else if /i "%SystemDrive%" neq "%OSDST%" (
	ECHO !DATE! !TIME![Log TRACE]  Running in the Offline, SET OSEnv=Offline >>%LogPath%
	SET OSEnv=Offline
) else (
	ECHO !DATE! !TIME![Log TRACE]  This is Online installation. >>%LogPath% 2>&1
)

if exist %OSDST%\OEM\Preload\Command\POP*.ini ECHO [Intel Intel Serial I/O Serial I/O]>> %OSDST%\OEM\Preload\OEMINFLIST.ini
for /f "tokens=*" %%v in (InfFiles.txt) do (
	if /i "%OSEnv%" equ "Offline" (
		ECHO !DATE! !TIME![Log TRACE]  DISM /image:%OSDST%\ /add-driver /driver:"%%v" >> %LogPath%
		DISM /image:%OSDST%\ /add-driver /driver:"%%v" >> %LogPath% 2>&1  		
    ) else (
		ECHO !DATE! !TIME![Log TRACE]  pnputil /add-driver "%%v" /install >> %LogPath%
		pnputil /add-driver "%%v" /install >> %LogPath% 2>&1
		ECHO !DATE! !TIME![Log TRACE]  pnputil -i -a "%%v" >> %LogPath%
		pnputil -i -a "%%v" >> %LogPath% 2>&1
	)
    ECHO.>>%LogPath%

    for /f "skip=1 tokens=2 delims=,;" %%s in ('find /i "DriverVer" "%%v"') do (
        if exist %OSDST%\OEM\Preload\Command\POP*.ini ECHO %%~nxv=%%s>> %OSDST%\OEM\Preload\OEMINFLIST.ini
    )
)
if exist %OSDST%\OEM\Preload\Command\POP*.ini ECHO.>> %OSDST%\OEM\Preload\OEMINFLIST.ini

ECHO %DATE% %TIME%[Log Leave]  ============ %~dpnx0 ============ >> %LogPath%
ECHO.>>%LogPath%

popd
SETLOCAL DISABLEDELAYEDEXPANSION
ECHO Install finished
