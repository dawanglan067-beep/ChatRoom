@echo off
setlocal

cd /d "%~dp0"

where node >nul 2>&1
if errorlevel 1 (
  echo [ERROR] Node.js not found in PATH.
  echo Please install Node.js LTS, then reopen this script.
  pause
  exit /b 1
)

if not exist "node_modules" (
  echo [INFO] Installing dependencies...
  call npm install
  if errorlevel 1 (
    echo [ERROR] npm install failed.
    pause
    exit /b 1
  )
)

set "HAS_JWT_SECRET="
if defined JWT_SECRET set "HAS_JWT_SECRET=1"
if not defined HAS_JWT_SECRET if exist ".env" (
  findstr /R /C:"^[ ]*JWT_SECRET[ ]*=" ".env" >nul 2>&1
  if not errorlevel 1 set "HAS_JWT_SECRET=1"
)

if not defined HAS_JWT_SECRET (
  echo [WARN] JWT_SECRET was not found in environment or .env.
  echo This project requires JWT_SECRET to start.
  set /p JWT_SECRET=Enter JWT_SECRET now: 
)

if not defined JWT_SECRET if not defined HAS_JWT_SECRET (
  echo [ERROR] JWT_SECRET is empty. Startup cancelled.
  pause
  exit /b 1
)

echo [INFO] Starting backend server...
call npm start
set "EXIT_CODE=%ERRORLEVEL%"

if not "%EXIT_CODE%"=="0" (
  echo [ERROR] Backend exited with code %EXIT_CODE%.
)

pause
exit /b %EXIT_CODE%
