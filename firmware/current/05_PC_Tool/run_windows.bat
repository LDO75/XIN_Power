@echo off
setlocal
cd /d "%~dp0"

set "PYTHON_CMD="
py -3 -c "import sys" >nul 2>nul
if not errorlevel 1 set "PYTHON_CMD=py -3"
if not defined PYTHON_CMD (
  python -c "import sys" >nul 2>nul
  if not errorlevel 1 set "PYTHON_CMD=python"
)
if not defined PYTHON_CMD (
  echo [ERROR] Python 3 not found.
  pause
  exit /b 1
)

if not exist .venv\Scripts\python.exe (
  %PYTHON_CMD% -m venv .venv
  if errorlevel 1 goto :failed
)
.venv\Scripts\python.exe -c "import PySide6, serial" >nul 2>nul
if errorlevel 1 (
  .venv\Scripts\python.exe -m pip install -r requirements.txt
  if errorlevel 1 goto :failed
)
.venv\Scripts\python.exe xin_power_qt.py
exit /b %errorlevel%

:failed
echo [ERROR] Dependency installation failed.
pause
exit /b 1
