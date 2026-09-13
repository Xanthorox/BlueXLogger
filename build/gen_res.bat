@echo off
rem BlueXLogger - build-time resource generators (icon + config slot placeholder)
rem Created by Xencode-CLI by xanthorox
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0.."
if not exist build\bin mkdir build\bin
if not exist res mkdir res

cl /nologo /W4 /O2 /D_CRT_SECURE_NO_WARNINGS /Fobuild\bin\ /Febuild\bin\gen_slot.exe tools\gen_slot.c
if errorlevel 1 goto :fail

cl /nologo /W4 /O2 /D_CRT_SECURE_NO_WARNINGS /Fobuild\bin\ /Febuild\bin\gen_ico.exe tools\gen_ico.c
if errorlevel 1 goto :fail

build\bin\gen_slot.exe res\cfg_slot.bin
if errorlevel 1 goto :fail

build\bin\gen_ico.exe res\bluexlogger.ico
if errorlevel 1 goto :fail

echo GENRES_OK
endlocal
exit /b 0

:fail
echo GENRES_FAILED
endlocal
exit /b 1
