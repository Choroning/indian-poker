@echo off
REM Indian Poker - Windows launcher
REM Double-click to start both the WebSocket server and the static client server.

setlocal
cd /d "%~dp0"

set "SERVER_BIN=server\build\ip_server.exe"
set "CLIENT_DIR=client"
set "WS_PORT=8080"
set "STATIC_PORT=3000"

REM -- Preflight checks -----------------------------------
if not exist "%SERVER_BIN%" (
    echo [ERROR] WebSocket server binary not found at %SERVER_BIN%
    echo         Build it first:
    echo           cmake -S server -B server\build
    echo           cmake --build server\build
    echo.
    pause
    exit /b 1
)

where python >nul 2>nul
if errorlevel 1 (
    echo [ERROR] python not found on PATH ^(needed for static file server^)
    echo.
    pause
    exit /b 1
)

REM -- Kill anything already on those ports --------------
for /f "tokens=5" %%a in ('netstat -ano ^| findstr :%WS_PORT% ^| findstr LISTENING') do taskkill /F /PID %%a >nul 2>nul
for /f "tokens=5" %%a in ('netstat -ano ^| findstr :%STATIC_PORT% ^| findstr LISTENING') do taskkill /F /PID %%a >nul 2>nul

REM -- Start servers --------------------------------------
echo Starting WebSocket server on :%WS_PORT%
start "Indian Poker - WebSocket" "%SERVER_BIN%"

echo Starting static file server on :%STATIC_PORT%
start "Indian Poker - Static" cmd /k python -m http.server %STATIC_PORT% --directory %CLIENT_DIR%

REM -- Wait a moment for servers to boot ------------------
timeout /t 2 /nobreak >nul

echo.
echo ==========================================
echo   Indian Poker server running
echo   - http://localhost:%STATIC_PORT%
echo ==========================================
echo.
echo Opening browser...
start "" "http://localhost:%STATIC_PORT%"

echo.
echo Close the two opened server windows to stop.
echo.
pause
