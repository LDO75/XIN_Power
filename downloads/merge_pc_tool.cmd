@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0merge_pc_tool.ps1"
if errorlevel 1 (
  echo Merge failed. Read the error above.
  pause
  exit /b 1
)
echo Merge completed.
pause
