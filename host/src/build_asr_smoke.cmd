@echo off
setlocal
set "VCVARS="
for %%P in (
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) do if not defined VCVARS if exist %%P set "VCVARS=%%~P"
if not defined VCVARS (echo VCVARS_NOT_FOUND & exit /b 1)
call "%VCVARS%" >nul 2>&1
if errorlevel 1 (echo VCVARS_FAIL & exit /b 1)
cd /d "%~dp0"
cl /nologo /W4 /utf-8 /Fe:asr_smoke.exe asr_smoke_test.c asr_transport.c asr_adapter.c winhttp.lib
if errorlevel 1 exit /b 1
echo BUILD_OK
