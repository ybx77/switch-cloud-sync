@echo off
cd /d "%~dp0"
set PYTHONIOENCODING=utf-8
echo Starting Switch Cloud Sync server...
echo Open http://127.0.0.1:8420/ in your browser.
python switchcloud_server.py --host 0.0.0.0 --port 8420
pause
