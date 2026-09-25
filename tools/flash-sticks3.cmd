@echo off
rem StickS3 USB flash (REL-02; upgrade keeps NVS/bond). Usage: flash-sticks3.cmd [COMx]
setlocal
set PORT=%1
if "%PORT%"=="" set PORT=COM5
set PY=python
set "ART=%~dp0..\flash-artifacts"
"%PY%" -m esptool --chip esp32s3 -p %PORT% --baud 921600 --before default-reset --after hard-reset write-flash 0x0 "%ART%\bootloader.bin" 0x8000 "%ART%\partition-table.bin" 0x10000 "%ART%\sticks3.bin"
