@echo off
rem Double-click to play. Arguments are passed on (e.g. basara.bat -Build).
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0basara.ps1" %*
if errorlevel 1 pause
