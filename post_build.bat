@echo off
setlocal

set "TARGET=%~1"
if "%TARGET%"=="" (
    exit /b 0
)

where FileSignerCli >nul 2>nul
if errorlevel 1 (
    exit /b 0
)

FileSignerCli -p ev -t -v -m override "%TARGET%" -o "%TARGET%"
exit /b %errorlevel%
