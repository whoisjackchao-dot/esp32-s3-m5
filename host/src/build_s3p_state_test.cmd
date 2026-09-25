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
cl /nologo /W4 /utf-8 /Fe:test_s3p_state.exe s3p_state_test.c "..\..\firmware\sticks3\main\s3p_state.c" /I"..\..\firmware\sticks3\main"
if errorlevel 1 exit /b 1
test_s3p_state.exe
