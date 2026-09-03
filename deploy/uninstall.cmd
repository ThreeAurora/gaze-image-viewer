@echo off
rem ── Gaze 卸载脚本:删快捷方式、清注册表、删安装目录 ──
setlocal
set "DEST=%LOCALAPPDATA%\Programs\Gaze"
if not defined DEST set "DEST=%USERPROFILE%\AppData\Local\Programs\Gaze"

del /q "%USERPROFILE%\Desktop\Gaze.lnk" 2>nul
del /q "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Gaze.lnk" 2>nul
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Gaze" /f >nul 2>&1
rd /s /q "%DEST%" 2>nul
if exist "%DEST%" (echo Gaze partially removed; please delete "%DEST%" manually) else echo Gaze has been removed.
endlocal