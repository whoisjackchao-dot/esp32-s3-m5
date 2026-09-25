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

rem ble_client needs WinRT/cppwinrt. This machine only has SDK 10.0.19041 whose bundled
rem cppwinrt uses <experimental/coroutine> (clashes with /std:c++20). Use generated modern
rem cppwinrt headers (LOCALAPPDATA sticks3 winrt-headers) with /std:c++17 /await instead.
set "CPPWINRT_GEN=%LOCALAPPDATA%\sticks3\winrt-headers"
if not exist "%CPPWINRT_GEN%\winrt\base.h" (
  echo ERROR: modern cppwinrt headers missing: %CPPWINRT_GEN%\winrt\base.h
  echo Generate them ^(no admin required^):
  echo   1^) get cppwinrt.exe from NuGet Microsoft.Windows.CppWinRT into %LOCALAPPDATA%\sticks3\
  echo   2^) "%LOCALAPPDATA%\sticks3\cppwinrt.exe" -input "%WindowsSdkDir%UnionMetadata\10.0.19041.0\Windows.winmd" -reference "%WindowsSdkDir%References\10.0.19041.0" -output "%CPPWINRT_GEN%"
  exit /b 1
)
cl /nologo /W4 /utf-8 /std:c++17 /await /EHsc /I"%CPPWINRT_GEN%" /Fe:ble_client.exe ble_client.cpp s3p_reasm.c state_merge.c "..\..\firmware\sticks3\main\s3p_state.c" /I"..\..\firmware\sticks3\main" /link windowsapp.lib
if errorlevel 1 exit /b 1
echo BUILD_OK
