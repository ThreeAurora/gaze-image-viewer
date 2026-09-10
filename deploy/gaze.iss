; ═══════════════════════════════════════════════════════
; Gaze 安装版脚本(Inno Setup 6)。由 deploy/make_installer.ps1 调 ISCC 编译:
;   ISCC /DAppVersion=X.Y.Z deploy/gaze.iss
;   X.Y.Z 由 make_installer.ps1 从 src/constants.h 的 GAZE_VERSION 读出后注入;
;   下方 #define 只是绕过脚本、直接调 ISCC 时的兜底,正常发版不必改这里。
; 语义: 装进 {autopf}\Gaze;exe 旁只预置 [Integration] iniLocation=1 引导
; ini(settings.cpp resolveIniPath 首启即把它拷去 %APPDATA% 当主配置起点),
; 程序目录只读不写;卸载只删 {app},用户配置/缓存留在 %APPDATA%。
; ═══════════════════════════════════════════════════════
#ifndef AppVersion
#define AppVersion "1.0.0"
#endif

[Setup]
AppId={{7C1E9A42-3D5B-4F68-8A0C-91E2B3D4F5A6}
AppName=Gaze
AppVersion={#AppVersion}
AppPublisher=ThreeAurora
VersionInfoVersion={#AppVersion}
VersionInfoProductVersion={#AppVersion}
DefaultDirName={autopf}\Gaze
DefaultGroupName=Gaze
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=Gaze_{#AppVersion}_Setup
SetupIconFile=..\src\gaze.ico
UninstallDisplayIcon={app}\Gaze.exe
CloseApplications=yes
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\dist\GazePortable\*"; DestDir: "{app}"; \
    Flags: recursesubdirs createallsubdirs ignoreversion
Source: "bootstrap.ini"; DestDir: "{app}"; DestName: "Gaze.ini"; \
    Flags: onlyifdoesntexist

[Icons]
Name: "{group}\Gaze"; Filename: "{app}\Gaze.exe"
Name: "{autodesktop}\Gaze"; Filename: "{app}\Gaze.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Gaze.exe"; Description: "{cm:LaunchProgram,Gaze}"; \
    Flags: nowait postinstall skipifsilent
