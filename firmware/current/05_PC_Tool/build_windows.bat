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
  echo [ERROR] Python 3 not found. Install Python 3.11 or newer first.
  pause
  exit /b 1
)

if not exist .venv\Scripts\python.exe (
  %PYTHON_CMD% -m venv .venv
  if errorlevel 1 goto :failed
)

call .venv\Scripts\activate.bat
python -m pip install --upgrade pip
if errorlevel 1 goto :failed
python -m pip install -r requirements.txt
if errorlevel 1 goto :failed
python -m pip install "pyinstaller>=6.10,<7"
if errorlevel 1 goto :failed
python -m PyInstaller --noconfirm --clean XINPowerConsole.spec
if errorlevel 1 goto :failed

echo.
echo Build complete: dist\XIN_Power_Studio_V6.2.3.exe
pause
exit /b 0

:failed
echo.
echo [ERROR] Build failed.
pause
exit /b 1
