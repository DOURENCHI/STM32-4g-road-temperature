@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo ================================================================
echo  Building and running unit tests...
echo ================================================================
gcc -std=c99 -Wall -Wextra -O1 -o test_app.exe ../App/app_dht_decode.c ../App/app_protocol.c ../App/app_ringbuf.c ../App/app_retrans.c test_dht_decode.c test_protocol.c test_ringbuf.c test_retrans.c test_integration.c test_main.c
if errorlevel 1 goto buildfail
echo Build OK.
echo.
test_app.exe
echo.
echo ----------------------------------------------------------------
pause
exit /b 0

:buildfail
echo.
echo BUILD FAILED - check that gcc is on PATH.
pause
exit /b 1
