@echo off
setlocal
REM ONE-COMMAND BUILD. Needs C:\msys64 once (see README).

set T=%~dp0scripts\build_menu_bootloader_native.cmd
if not exist "%T%" set T=%~dp0nano3_bootloader\scripts\build_menu_bootloader_native.cmd

if not exist "%T%" (
    echo [ERR] Cannot find scripts\build_menu_bootloader_native.cmd
    echo        Expected at:  %~dp0scripts\
    echo        Re-copy the whole nano3_bootloader folder there ^(with its
    echo        scripts\, bootloader\, payloads\ subfolders^).
    pause & exit /b 1
)
call "%T%" %*
