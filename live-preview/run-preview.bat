@echo off
cd /d "%~dp0"
where py >NUL 2>&1
if %errorlevel%==0 (py -3 serve.py %*) else (python serve.py %*)
pause
