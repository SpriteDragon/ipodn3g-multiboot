@echo off
setlocal EnableDelayedExpansion
cd /d %~dp0
title Push bootloader to the iPod

set EXE=
if exist mks5lboot.exe         set EXE=mks5lboot.exe
if exist scripts\mks5lboot.exe set EXE=scripts\mks5lboot.exe
if exist tools\mks5lboot.exe   set EXE=tools\mks5lboot.exe
where mks5lboot.exe >nul 2>nul && set EXE=mks5lboot.exe
if "%EXE%"=="" (
    echo [ERR] Put mks5lboot.exe next to this script and run again.
    pause & exit /b 1
)

set IMG=
for %%f in (out\*.ipod) do set IMG=%%f
if "%IMG%"=="" (
    echo [ERR] Nothing built yet. Run build.cmd first.
    pause & exit /b 1
)

echo  iPod: hold MENU+SELECT until the screen is fully black ^(DFU^), keep cable in.
echo  Image: %IMG%
echo.

:go
"%EXE%" --bl-inst "%IMG%"
if errorlevel 1 (
    echo.
    echo  Failed. Usual causes: not really in DFU ^(screen must be black^),
    echo  missing WinUSB driver ^(Zadig^), or not run as Administrator.
    echo  Re-enter DFU mode, then press any key to retry.
    pause >nul
    goto go
)
echo.
echo  Done -- double beep means it flashed, iPod is rebooting into the boot menu.
pause
