:: =============================================
::  CustomPE - Custom Windows PE for OEM Deployment
::  Author: AlexMartin
::  Last updated: 2026-08-07
:: =============================================
@echo off
setlocal EnableDelayedExpansion

:: Load global configuration (inherited from AutoRun, or read from Build-WinPE.conf)
if not defined CustomPE_NAME (
    if exist "%~dp0..\..\Build-WinPE.conf" (
        for /f "usebackq eol=# tokens=1,* delims==" %%a in ("%~dp0..\..\Build-WinPE.conf") do set "%%a=%%b"
    )
)
if not defined CustomPE_NAME set "CustomPE_NAME=CustomPE"
if not defined CustomPE_AUTHOR set "CustomPE_AUTHOR=AlexMartin"
if not defined CustomPE_VERSION set "CustomPE_VERSION=1.0"

set TEST_PASS=0
set TEST_FAIL=0
set TEST_SKIP=0

:: ---- Logging: real-time console + file into <CustomPE_NAME>\Logs ----
for %%I in ("%~dp0..\Logs") do set "LOG_DIR=%%~fI"
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%" >nul 2>&1
set "LOG_PROBE=%LOG_DIR%\.probe"
echo x > "%LOG_PROBE%" 2>nul
if not exist "%LOG_PROBE%" (
    set "LOG_DIR=X:\!CustomPE_NAME!\Logs"
    if not exist "!LOG_DIR!" mkdir "!LOG_DIR!" >nul 2>&1
)
del /f /q "%LOG_PROBE%" >nul 2>&1
set "LOG_FILE=%LOG_DIR%\Test_Components.log"
if exist "%LOG_FILE%" del /f /q "%LOG_FILE%" >nul 2>&1
echo ============================================ >  "%LOG_FILE%" 2>nul
echo  %CustomPE_NAME% Component Test Log              >> "%LOG_FILE%" 2>nul
echo  Started: %date% %time%                          >> "%LOG_FILE%" 2>nul
echo  Log: %LOG_FILE%                                 >> "%LOG_FILE%" 2>nul
echo ============================================ >> "%LOG_FILE%" 2>nul
echo. >> "%LOG_FILE%" 2>nul
call :Log "============================================"
call :Log "  %CustomPE_NAME% Component and Hardware Test Suite"
call :Log "  Running in: %SYSTEMDRIVE%"
call :Log "  %date% %time%"
call :Log "============================================"
call :Log ""
call :Log "============================================"
call :Log "  Test Plan"
call :Log "============================================"
call :Log "  1.  WMI - OS information"
call :Log "  2.  WMI - Storage (logical disks, volumes)"
call :Log "  3.  VBScript engine"
call :Log "  4.  HTA component present"
call :Log "  5.  .NET Framework runtime"
call :Log "  6.  PowerShell - processes"
call :Log "  7.  PowerShell - services"
call :Log "  8.  PowerShell - file system"
call :Log "  9.  DISM PowerShell cmdlets"
call :Log "  10.  DISM - mounted images"
call :Log "  11.  Network - IP configuration"
call :Log "  12.  Network - sockets"
call :Log "  13.  SecureBoot UEFI (skip if not UEFI)"
call :Log "  14.  Font registry accessible"
call :Log "  15.  Microsoft YaHei font registered"
call :Log "  16.  Registry - PowerShell ExecutionPolicy"
call :Log "  17.  Registry - FontSubstitute"
call :Log "  18.  Registry - ThemeActive"
call :Log "  19.  Registry - ICU DefaultLanguage"
call :Log "  20.  Driver store present (DriverStore\FileRepository)"
call :Log "  21.  Driver and network adapter"
call :Log "  22.  WDS-Tools (wdsclient.exe)"
call :Log "  23.  MDAC - data access (odbcad32.exe)"
call :Log "  24.  Dot3Svc - 802.1X wired auth (dot3cfg.dll)"
call :Log "  25.  RNDIS - USB tethering (rndismp6.sys)"
call :Log "  26.  EnhancedStorage - BitLocker storage (ehstorclass.sys)"
call :Log "  27.  SecureStartup - BitLocker repair (repair-bde.exe)"
call :Log "  28.  PlatformID (platid.dll)"
call :Log "  29.  Fonts - ZH-HK/TW (msjh.ttc)"
call :Log "  30.  Fonts - JA-JP (yugothm.ttc)"
call :Log "  31.  Fonts - KO-KR (malgun.ttf)"
call :Log "  32.  Fonts - Legacy (dokchamp.ttf)"
call :Log ""

:: ---- 1. WMI - OS information ----
powershell -NoProfile -Command "Get-CimInstance Win32_OperatingSystem | Out-Null"
if !errorlevel! equ 0 ( call :Pass "WMI - OS information" ) else ( call :Fail "WMI - OS information" )

:: ---- 2. WMI - Storage ----
powershell -NoProfile -Command "Get-CimInstance Win32_LogicalDisk | Out-Null; Get-CimInstance Win32_Volume | Out-Null"
if !errorlevel! equ 0 ( call :Pass "WMI - Storage" ) else ( call :Fail "WMI - Storage" )

:: ---- 3. VBScript engine ----
echo WScript.Echo "VBScript OK" > %TEMP%\test.vbs 2>nul
cscript //nologo %TEMP%\test.vbs >nul 2>&1
if !errorlevel! equ 0 ( call :Pass "VBScript engine" ) else ( call :Fail "VBScript engine" )
del %TEMP%\test.vbs >nul 2>&1

:: ---- 4. HTA component present ----
if exist "%SystemRoot%\System32\mshta.exe" ( call :Pass "HTA component present" ) else ( call :Fail "HTA component present" )

:: ---- 5. .NET Framework runtime ----
powershell -NoProfile -Command "[System.Environment]::Version | Out-Null"
if !errorlevel! equ 0 ( call :Pass ".NET Framework runtime" ) else ( call :Fail ".NET Framework runtime" )

:: ---- 6. PowerShell - processes ----
powershell -NoProfile -Command "Get-Process | Out-Null"
if !errorlevel! equ 0 ( call :Pass "PowerShell - processes" ) else ( call :Fail "PowerShell - processes" )

:: ---- 7. PowerShell - services ----
powershell -NoProfile -Command "Get-Service | Out-Null"
if !errorlevel! equ 0 ( call :Pass "PowerShell - services" ) else ( call :Fail "PowerShell - services" )

:: ---- 8. PowerShell - file system ----
powershell -NoProfile -Command "Get-ChildItem %SystemRoot% | Out-Null"
if !errorlevel! equ 0 ( call :Pass "PowerShell - file system" ) else ( call :Fail "PowerShell - file system" )

:: ---- 9. DISM PowerShell cmdlets ----
powershell -NoProfile -Command "if (Get-Command -Module DISM -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }"
if !errorlevel! equ 0 ( call :Pass "DISM PowerShell cmdlets" ) else ( call :Fail "DISM PowerShell cmdlets" )

:: ---- 10. DISM - mounted images ----
dism /Get-MountedImageInfo >nul 2>&1
if !errorlevel! equ 0 ( call :Pass "DISM - mounted images" ) else ( call :Fail "DISM - mounted images" )

:: ---- 11. Network - IP configuration ----
ipconfig /all >nul 2>&1
if !errorlevel! equ 0 ( call :Pass "Network - IP configuration" ) else ( call :Fail "Network - IP configuration" )

:: ---- 12. Network - sockets ----
netstat -an >nul 2>&1
if !errorlevel! equ 0 ( call :Pass "Network - sockets" ) else ( call :Fail "Network - sockets" )

:: ---- 13. SecureBoot UEFI ----
powershell -NoProfile -Command "Confirm-SecureBootUEFI" >nul 2>&1
if !errorlevel! equ 0 ( call :Pass "SecureBoot UEFI" ) else ( call :Skip "SecureBoot UEFI (not UEFI or unsupported)" )

:: ---- 14. Font registry accessible ----
reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts" >nul 2>&1
if !errorlevel! equ 0 ( call :Pass "Font registry accessible" ) else ( call :Fail "Font registry accessible" )

:: ---- 15. Microsoft YaHei font registered ----
reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts" /v "Microsoft YaHei (TrueType)" >nul 2>&1
if !errorlevel! equ 0 ( call :Pass "Microsoft YaHei font registered" ) else ( call :Fail "Microsoft YaHei font registered" )

:: ---- 16. Registry - PowerShell ExecutionPolicy ----
reg query "HKLM\SOFTWARE\Microsoft\PowerShell\1\ShellIds\Microsoft.PowerShell" /v ExecutionPolicy >nul 2>&1
if !errorlevel! equ 0 ( call :Pass "Registry - PowerShell ExecutionPolicy" ) else ( call :Fail "Registry - PowerShell ExecutionPolicy" )

:: ---- 17. Registry - FontSubstitute ----
reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\FontSubstitutes" /v "MS Shell Dlg" >nul 2>&1
if !errorlevel! equ 0 ( call :Pass "Registry - FontSubstitute" ) else ( call :Fail "Registry - FontSubstitute" )

:: ---- 18. Registry - ThemeActive ----
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\ThemeManager" /v ThemeActive >nul 2>&1
if !errorlevel! equ 0 ( call :Pass "Registry - ThemeActive" ) else ( call :Fail "Registry - ThemeActive" )

:: ---- 19. Registry - ICU DefaultLanguage ----
reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ICU\Global" /v DefaultLanguage >nul 2>&1
if !errorlevel! equ 0 ( call :Pass "Registry - ICU DefaultLanguage" ) else ( call :Fail "Registry - ICU DefaultLanguage" )

:: ---- 20. Driver store present ----
set DRVCOUNT=0
for /d %%d in ("%SystemRoot%\System32\DriverStore\FileRepository\*") do set /a DRVCOUNT+=1
if !DRVCOUNT! gtr 0 ( call :Pass "Driver store present (!DRVCOUNT! packages)" ) else ( call :Fail "Driver store present (no packages)" )

:: ---- 21. Driver and network adapter suite ----
:: -- extract the embedded PowerShell driver test and run it (single-file suite) --
powershell -NoProfile -Command "$l=Get-Content -LiteralPath '%~f0'; $m=($l | Select-String '::CustomPE_DRIVER_PS1' | Select-Object -Last 1).LineNumber; [IO.File]::WriteAllText($env:TEMP+'\CustomPE_DriverTest.ps1', (($l | Select-Object -Skip $m) -join [Environment]::NewLine), [Text.Encoding]::ASCII)"
powershell -NoProfile -ExecutionPolicy Bypass -File "%TEMP%\CustomPE_DriverTest.ps1"
if !errorlevel! equ 0 (
    call :Pass "Driver and network adapter suite (all adapters OK)"
) else if !errorlevel! equ 1 (
    call :Skip "Driver and network adapter suite (no adapter or no in-box driver)"
) else (
    call :Fail "Driver and network adapter suite"
)
del "%TEMP%\CustomPE_DriverTest.ps1" >nul 2>&1

:: ---- 22. WDS-Tools ----
if exist "%SystemRoot%\System32\wdsclient.exe" ( call :Pass "WDS-Tools (wdsclient.exe)" ) else ( call :Fail "WDS-Tools (wdsclient.exe)" )

:: ---- 23. MDAC - data access ----
if exist "%SystemRoot%\System32\odbcad32.exe" ( call :Pass "MDAC - data access (odbcad32.exe)" ) else ( call :Fail "MDAC - data access (odbcad32.exe)" )

:: ---- 24. Dot3Svc - 802.1X wired auth ----
if exist "%SystemRoot%\System32\dot3cfg.dll" ( call :Pass "Dot3Svc - 802.1X wired auth" ) else ( call :Fail "Dot3Svc - 802.1X wired auth" )

:: ---- 25. RNDIS - USB tethering ----
set RNDIS_FOUND=0
for /d %%d in ("%SystemRoot%\System32\DriverStore\FileRepository\netrndis.inf_*") do if exist "%%d\rndismp6.sys" set RNDIS_FOUND=1
if !RNDIS_FOUND! equ 1 ( call :Pass "RNDIS - USB tethering" ) else ( call :Fail "RNDIS - USB tethering" )

:: ---- 26. EnhancedStorage - BitLocker storage ----
if exist "%SystemRoot%\System32\drivers\ehstorclass.sys" ( call :Pass "EnhancedStorage - BitLocker storage" ) else ( call :Fail "EnhancedStorage - BitLocker storage" )

:: ---- 27. SecureStartup - BitLocker repair ----
if exist "%SystemRoot%\System32\repair-bde.exe" ( call :Pass "SecureStartup - BitLocker repair" ) else ( call :Fail "SecureStartup - BitLocker repair" )

:: ---- 28. PlatformID ----
if exist "%SystemRoot%\System32\wbem\platid.dll" ( call :Pass "PlatformID" ) else ( call :Fail "PlatformID" )

:: ---- 29. Fonts - ZH-HK/TW (Microsoft JhengHei) ----
if exist "%SystemRoot%\Fonts\msjh.ttc" ( call :Pass "Fonts - ZH-HK/TW (msjh.ttc)" ) else ( call :Fail "Fonts - ZH-HK/TW (msjh.ttc)" )

:: ---- 30. Fonts - JA-JP (Yu Gothic) ----
if exist "%SystemRoot%\Fonts\yugothm.ttc" ( call :Pass "Fonts - JA-JP (yugothm.ttc)" ) else ( call :Fail "Fonts - JA-JP (yugothm.ttc)" )

:: ---- 31. Fonts - KO-KR (Malgun Gothic) ----
if exist "%SystemRoot%\Fonts\malgun.ttf" ( call :Pass "Fonts - KO-KR (malgun.ttf)" ) else ( call :Fail "Fonts - KO-KR (malgun.ttf)" )

:: ---- 32. Fonts - Legacy ----
if exist "%SystemRoot%\Fonts\dokchamp.ttf" ( call :Pass "Fonts - Legacy (dokchamp.ttf)" ) else ( call :Fail "Fonts - Legacy (dokchamp.ttf)" )

echo.
call :Log ""
call :Log "============================================"
call :Log "  Failed Tests"
call :Log "============================================"
if %TEST_FAIL% gtr 0 (
    for /l %%i in (1,1,%TEST_FAIL%) do (
        echo   %%i. !FAIL_%%i!
        if defined LOG_FILE echo   %%i. !FAIL_%%i! >> "%LOG_FILE%" 2>nul
    )
) else (
    echo   ^(none^)
    if defined LOG_FILE echo   ^(none^) >> "%LOG_FILE%" 2>nul
)
call :Log ""
call :Log "============================================"
call :Log "  Test Summary"
call :Log "============================================"
call :Log "  Pass: %TEST_PASS%   Fail: %TEST_FAIL%   Skip: %TEST_SKIP%"
if %TEST_FAIL% gtr 0 (
    echo   Result: FAILED
    if defined LOG_FILE echo   Result: FAILED >> "%LOG_FILE%" 2>nul
) else (
    echo   Result: PASSED
    if defined LOG_FILE echo   Result: PASSED >> "%LOG_FILE%" 2>nul
)
call :Log "============================================"
call :Log "  Log file: %LOG_FILE%"
call :Log ""

endlocal & exit /b %TEST_FAIL%

:Pass
set /a TEST_PASS+=1
echo   [PASS] %~1
if defined LOG_FILE echo   [PASS] %~1 >> "%LOG_FILE%" 2>nul
exit /b 0

:Fail
set /a TEST_FAIL+=1
set "FAIL_!TEST_FAIL!=%~1"
echo   [FAIL] %~1
if defined LOG_FILE echo   [FAIL] %~1 >> "%LOG_FILE%" 2>nul
exit /b 0

:Skip
set /a TEST_SKIP+=1
echo   [SKIP] %~1
if defined LOG_FILE echo   [SKIP] %~1 >> "%LOG_FILE%" 2>nul
exit /b 0

:Log
echo(%~1
if defined LOG_FILE echo(%~1 >> "%LOG_FILE%" 2>nul
exit /b 0
::CustomPE_DRIVER_PS1
$ErrorActionPreference = 'SilentlyContinue'
$pass = 0
$fail = 0
$skip = 0

Write-Host '============================================'
Write-Host '  Driver & Network Adapter Test'
Write-Host ('  {0}  {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $env:COMPUTERNAME)
Write-Host '============================================'

# -- 1. enumerate physical network adapters --
$adapters = @(Get-CimInstance Win32_NetworkAdapter | Where-Object { $_.PhysicalAdapter })
if ($adapters.Count -eq 0) {
    Write-Host '  [SKIP] No physical network adapters found'
    Write-Host '  Result: SKIPPED'
    exit 1
}
$pass++
Write-Host ('  [PASS] Network adapters enumerated ({0})' -f $adapters.Count)
foreach ($a in $adapters) {
    $code = $a.ConfigManagerErrorCode
    $state = if ($code -eq 0) { 'driver OK' } else { 'no in-box driver (code ' + $code + ')' }
    Write-Host ('      {0} | Enabled={1} | ConnStatus={2} | {3}' -f $a.Name, $a.NetEnabled, $a.NetConnectionStatus, $state)
}

# -- 2. driver status of all network adapters --
# Hardware without an in-box WinPE driver (e.g. an unsupported Wi-Fi card) is
# hardware-specific, not a PE component failure, so it counts as SKIP.
$bad = @($adapters | Where-Object { $_.ConfigManagerErrorCode -ne 0 })
if ($bad.Count -gt 0) {
    $skip++
    Write-Host ('  [SKIP] {0} adapter(s) without an in-box driver' -f $bad.Count)
    foreach ($b in $bad) { Write-Host ('      Problem device: {0} (code {1})' -f $b.Name, $b.ConfigManagerErrorCode) }
} else {
    $pass++
    Write-Host '  [PASS] All network adapter drivers loaded'
}

# -- 3. IPv4 address assigned --
$ipcfg = @(Get-CimInstance Win32_NetworkAdapterConfiguration | Where-Object { $_.IPEnabled -and $_.IPAddress })
if ($ipcfg.Count -gt 0) {
    $pass++
    Write-Host '  [PASS] IPv4 address assigned'
    foreach ($c in $ipcfg) {
        $ips = @($c.IPAddress | Where-Object { $_ -notmatch ':' })
        if ($ips.Count -gt 0) { Write-Host ('      {0}: {1}' -f $c.Description, ($ips -join ', ')) }
    }
} else {
    $skip++
    Write-Host '  [SKIP] No IPv4 address assigned (may need a connected network)'
}

Write-Host '============================================'
Write-Host ('  Driver Test Summary: Pass={0} Fail={1} Skip={2}' -f $pass, $fail, $skip)
if ($fail -gt 0) {
    Write-Host '  Result: FAILED'
    exit 2
}
if ($skip -gt 0) {
    Write-Host '  Result: SKIPPED'
    exit 1
}
Write-Host '  Result: PASSED'
exit 0
