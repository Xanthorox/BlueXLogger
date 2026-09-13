@echo off
rem ===========================================================================
rem BlueXLogger - build/preflight.bat
rem Detects the toolchain and prints the exact versions and paths the build
rem depends on. Run this first on a new machine: build.bat pins the same
rem locations, and a mismatch here explains a build failure there.
rem Created by Xencode-CLI by xanthorox
rem ===========================================================================
setlocal enabledelayedexpansion

echo.
echo === BlueXLogger toolchain preflight =====================================
echo   BlueXLogger - Created by Xencode-CLI by xanthorox
echo.

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" goto :no_vcvars
echo   [ ok ] vcvars64.bat   %VCVARS%

call "%VCVARS%" >nul 2>&1
if errorlevel 1 goto :bad_vcvars

where cl.exe >nul 2>&1
if errorlevel 1 goto :no_cl
for /f "tokens=*" %%v in ('cl 2^>^&1 ^| findstr /r /c:"Version"') do echo   [ ok ] cl.exe          %%v

where link.exe >nul 2>&1
if errorlevel 1 goto :no_link
echo   [ ok ] link.exe        found

set "RCDIR=C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64"
if exist "%RCDIR%\rc.exe" echo   [ ok ] rc.exe          %RCDIR%\rc.exe
if not exist "%RCDIR%\rc.exe" echo   [WARN] rc.exe not in the 10.0.22621.0 bin directory

set "SDKLIB=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64"
if exist "%SDKLIB%\secur32.lib" echo   [ ok ] Windows SDK    10.0.22621.0, secur32 + crypt32 + windowscodecs
if not exist "%SDKLIB%\secur32.lib" echo   [WARN] SDK 10.0.22621.0 lib directory not found, another installed SDK will be used

echo.
echo   --- source tree ---
pushd "%~dp0.."
call :check include\bxl_common.h
call :check src\common\bxl_net.c
call :check src\common\bxl_smtp.c
call :check src\payload\main.c
call :check src\builder\main.c
call :check tests\run_tests.bat
call :check res\payload.rc
call :check res\builder.rc
popd

echo.
echo   PREFLIGHT_OK
endlocal
exit /b 0

:check
if exist "%~1" echo   [ ok ] %~1
if not exist "%~1" echo   [WARN] missing %~1
exit /b 0

:no_vcvars
echo   [FAIL] vcvars64.bat not found at:
echo          %VCVARS%
echo          Install Visual Studio 2022 Build Tools with the
echo          "Desktop development with C++" workload.
goto :fail

:bad_vcvars
echo   [FAIL] vcvars64.bat failed to initialise the environment
goto :fail

:no_cl
echo   [FAIL] cl.exe not on PATH after vcvars64
goto :fail

:no_link
echo   [FAIL] link.exe not on PATH after vcvars64
goto :fail

:fail
echo.
echo   PREFLIGHT_FAILED
endlocal
exit /b 1
