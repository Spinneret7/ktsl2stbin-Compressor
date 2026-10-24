@echo off
setlocal
cd /d "%~dp0"

echo Building encoder...
gcc -O2 -Wall -Wextra -static -municode -s -o ktsl2stbin_enc.exe ktsl2stbin_enc.c ktsz_common.c
if errorlevel 1 exit /b 1

echo Building decoder...
gcc -O2 -Wall -Wextra -static -municode -s -o ktsl2stbin_dec.exe ktsl2stbin_dec.c ktsz_common.c
if errorlevel 1 exit /b 1

echo.
echo Built ktsl2stbin_enc.exe and ktsl2stbin_dec.exe
