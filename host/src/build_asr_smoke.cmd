@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "C:\Temp\ESP32-S3M5\host\src"
cl /nologo /W4 /Fe:asr_smoke.exe asr_smoke_test.c asr_transport.c asr_adapter.c winhttp.lib
if errorlevel 1 exit /b 1
echo BUILD_OK
