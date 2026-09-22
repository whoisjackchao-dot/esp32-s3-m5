@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "C:\Temp\ESP32-S3M5\host\src"
cl /nologo /W4 /Fe:test_asr_adapter.exe asr_adapter_test.c asr_adapter.c
if errorlevel 1 exit /b 1
test_asr_adapter.exe
