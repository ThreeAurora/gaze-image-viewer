@echo off
rem ── Gaze 安装脚手架:由 Setup.exe 解压后自动执行 ──
rem 安装到用户目录(免管理员权限),配置相对 exe 路径自带,无需改任何代码。
rem 目标: %LOCALAPPDATA%\Programs\Gaze
setlocal
set "DEST=%LOCALAPPDATA%\Programs\Gaze"
if not defined DEST set "DEST=%USERPROFILE%\AppData\Local\Programs\Gaze"

if not exist "%DEST%" mkdir "%DEST%"
rem 把解压目录(含 install.cmd 自身)整树拷进安装目录
xcopy "%~dp0*" "%DEST%\" /E /I /Y /H /Q >nul

rem 桌面 + 开始菜单快捷方式(COM IShellLink,免管理员)
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$d='%DEST%'; $ws=New-Object -ComObject WScript.Shell; $s=$ws.CreateShortcut([Environment]::GetFolderPath('Desktop')+'\Gaze.lnk'); $s.TargetPath=$d+'\Gaze.exe'; $s.WorkingDirectory=$d; $s.Save(); $m=$ws.CreateShortcut([Environment]::GetFolderPath('StartMenu')+'\Programs\Gaze.lnk'); $m.TargetPath=$d+'\Gaze.exe'; $m.WorkingDirectory=$d; $m.Save();"

rem 注册到"设置 > 应用",可卸载
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Gaze" /v DisplayName /d "Gaze" /f >nul
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Gaze" /v DisplayVersion /d "1.0" /f >nul
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Gaze" /v Publisher /d "Gaze Project" /f >nul
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Gaze" /v InstallLocation /d "%DEST%" /f >nul
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Gaze" /v UninstallString /d "cmd /c \"\"%DEST%\uninstall.cmd\"\"" /f >nul

start "" "%DEST%\Gaze.exe"
endlocal