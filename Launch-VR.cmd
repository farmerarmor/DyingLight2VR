@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-VR.ps1"
if errorlevel 1 pause
