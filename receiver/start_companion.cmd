@echo off
cd /d "%~dp0"
if not exist server.py (
  echo Put these companion files beside your existing server.py and .env first.
  pause
  exit /b 1
)
python start_companion.py
pause
