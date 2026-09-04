# ═══════════════════════════════════════════════════════
# 安装版 Setup.exe 打包:Inno Setup 6(ISCC)编译 deploy/gaze.iss。
#   用法: powershell -ExecutionPolicy Bypass -File deploy/make_installer.ps1 [-Version 1.0.0]
#
# 产物链: dist/GazePortable(便携集合,由 make_portable.ps1 生成)
#         + deploy/bootstrap.ini([Integration] iniLocation=1 引导)
#      ->  ISCC 压成 dist/Gaze_<ver>_Setup.exe
# 安装语义: 装进 Program Files\Gaze(管理员一次到位);exe 旁只留引导 ini,
#           用户配置/缓存落 %APPDATA%(卸载保留,与便携版 exe 相对存放分流)。
# ═══════════════════════════════════════════════════════
param([string]$Version = "1.0.0")
if ([string]::IsNullOrWhiteSpace($Version)) { $Version = "1.0.0" }

$ErrorActionPreference = "Stop"
$root  = Split-Path $PSScriptRoot -Parent
$dist  = Join-Path $root "dist"
$base  = Join-Path $dist "GazePortable"
$setup = Join-Path $dist "Gaze_${Version}_Setup.exe"

if (!(Test-Path (Join-Path $base "Gaze.exe"))) {
    Write-Host "dist\GazePortable 不存在,先跑 deploy/make_portable.ps1"
    exit 1
}

$isccCandidates = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (!$iscc) { Write-Error "找不到 ISCC.exe,请安装 Inno Setup 6" }

# ISCC 编译(版本经 /DAppVersion 注入 gaze.iss,包名随之 Gaze_<ver>_Setup)
& $iscc "/DAppVersion=$Version" (Join-Path $PSScriptRoot "gaze.iss")
if ($LASTEXITCODE -ne 0) { Write-Error "ISCC 编译失败(exit $LASTEXITCODE)" }
if (!(Test-Path $setup)) { Write-Error "ISCC 未产出 $setup,检查 gaze.iss" }
$sz = [math]::Round((Get-Item $setup).Length / 1MB, 1)
Write-Host "Installer OK: $setup ($sz MB)"
