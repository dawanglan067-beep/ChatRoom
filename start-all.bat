@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

set "BACKEND_BAT=%~dp0backend\start-backend.bat"
if not exist "%BACKEND_BAT%" (
  echo [ERROR] Backend launcher not found:
  echo         %BACKEND_BAT%
  pause
  exit /b 1
)

set "CLIENT_EXE="
if exist "%~dp0build\Desktop_Qt_6_8_3_MinGW_64_bit-Debug\ChatRoom.exe" (
  set "CLIENT_EXE=%~dp0build\Desktop_Qt_6_8_3_MinGW_64_bit-Debug\ChatRoom.exe"
)

if not defined CLIENT_EXE (
  for /r "%~dp0build" %%F in (ChatRoom.exe) do (
    if /I not "%%~nxF"=="ChatRoomUnitTests.exe" (
      set "CLIENT_EXE=%%~fF"
      goto :found_client
    )
  )
)

:found_client
if not defined CLIENT_EXE (
  echo [ERROR] ChatRoom.exe not found under build\.
  echo Build first, then run this script again.
  pause
  exit /b 1
)

echo [INFO] Starting backend in a new window...
start "ChatRoom Backend" cmd /k "cd /d ""%~dp0backend"" && call ""%BACKEND_BAT%"""

timeout /t 2 >nul

echo [INFO] Starting client:
echo        %CLIENT_EXE%
start "ChatRoom Client" "%CLIENT_EXE%"

echo [INFO] Launch complete.
echo Close this window if you don't need it.
pause
exit /b 0
