@echo off
rem =============================================
rem  Rebuild MinimizeWindow.exe (x64, static CRT)
rem  Requires: Visual Studio 2022 + Windows SDK
rem =============================================
set "MSVC=D:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207"
set "SDK=D:\Windows Kits\10"
set "VER=10.0.26100.0"
set "INCLUDE=%MSVC%\include;%SDK%\Include\%VER%\ucrt;%SDK%\Include\%VER%\um;%SDK%\Include\%VER%\shared"
set "LIB=%MSVC%\lib\x64;%SDK%\Lib\%VER%\ucrt\x64;%SDK%\Lib\%VER%\um\x64"
"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /O1 /MT /GL /W3 MinimizeWindow.c /Fe:MinimizeWindow.exe /link user32.lib
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)
echo [OK] MinimizeWindow.exe rebuilt.