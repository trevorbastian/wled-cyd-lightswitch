@echo off
rem Launcher for cyd.py on Windows. Usage: cyd --help
where py >nul 2>nul
if %ERRORLEVEL%==0 (
    py -3 "%~dp0cyd.py" %*
) else (
    python "%~dp0cyd.py" %*
)
exit /b %ERRORLEVEL%
