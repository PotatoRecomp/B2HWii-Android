@echo off
rem Test: display lists decoded per vertex (BASARA_DL_SOFT).
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0basara.ps1" -DlSoft %*
if errorlevel 1 pause
