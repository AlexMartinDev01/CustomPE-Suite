@echo off
setlocal

rem ============================================
rem  CustomPE - One-click PE build
rem  Auto-elevates, then calls CustomPE\Build-WinPE.cmd
rem ============================================
net session >nul 2>&1
if errorlevel 1 (
    echo Requesting administrator privileges...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)

call "%~dp0CustomPE\Build-WinPE.cmd"
echo.
echo ============================================
echo  Build finished. ISO is under Build-WorkDir\ISO.
echo ============================================
pause
endlocal
