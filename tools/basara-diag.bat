@echo off
rem Test: geometry diagnostics log (BASARA_LOG_VPOS: position arrays + DL trace).
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0basara.ps1" -VPos %*
if errorlevel 1 pause
