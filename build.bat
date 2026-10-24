@echo off
setlocal
cd /d "%~dp0"

echo Building embedded resources...
windres -I. -O coff -o enc_resources.o enc_resources.rc
if errorlevel 1 exit /b 1
windres -I. -O coff -o dec_resources.o dec_resources.rc
if errorlevel 1 exit /b 1

echo Building encoder...
gcc -O2 -Wall -Wextra -static -municode -s -o ktsl2stbin_enc.exe ktsl2stbin_enc.c ktsz_common.c enc_resources.o
if errorlevel 1 exit /b 1

echo Building decoder...
gcc -O2 -Wall -Wextra -static -municode -s -o ktsl2stbin_dec.exe ktsl2stbin_dec.c ktsz_common.c dec_resources.o
if errorlevel 1 exit /b 1

del /q enc_resources.o dec_resources.o 2>nul

echo.
echo Built self-contained ktsl2stbin_enc.exe and ktsl2stbin_dec.exe
