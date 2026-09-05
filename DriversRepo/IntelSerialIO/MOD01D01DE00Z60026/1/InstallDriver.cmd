
@echo off
if not exist C:\OEM\AcerLogs md C:\OEM\AcerLogs
SET LogPath=C:\OEM\AcerLogs\WinPEREDrivers.log
ECHO.>>%LogPath%
ECHO %DATE% %TIME%[Log START]  ============ %~dpnx0 ============ >> %LogPath%
pushd "%~dp0"

ECHO %DATE% %TIME%[Log TRACE]  dism /image:C:\ /add-driver /driver:.\ /recurse >>%LogPath% 2>&1
dism /image:C:\ /add-driver /driver:.\ /recurse >>%LogPath% 2>&1


for /f "delims=" %%D in ('powershell get-date -format "{yyyyMMddHHmmss}"') do SET CurrentDateTime=%%D
ECHO %DATE% %TIME%[Log TRACE]  dir /b *.enc >> %LogPath%
for /f "tokens=1 delims=." %%F in ('dir /b *.enc') do set DRVDir=%%F_%CurrentDateTime%

ECHO %DATE% %TIME%[Log TRACE]  xcopy .\*.* "c:\OEM\Preload\MSDRV\%DRVDir%\*.*" /vesyf >>%LogPath% 2>&1
xcopy .\*.* "c:\OEM\Preload\MSDRV\%DRVDir%\*.*" /vesyf >>%LogPath% 2>&1

popd
ECHO %DATE% %TIME%[Log LEAVE]  ============ %~dpnx0 ============ >> %LogPath%
ECHO.>>%LogPath%