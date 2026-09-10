# ═══════════════════════════════════════════════════════
# 安装版 Setup.exe 打包:Inno Setup 6(ISCC)编译 deploy/gaze.iss。
#   用法: powershell -ExecutionPolicy Bypass -File deploy/make_installer.ps1 [-Version X.Y.Z]
#         不传 -Version 时,版本号取自 src/constants.h 的 GAZE_VERSION(单一来源)
#
# 产物链: dist/GazePortable(便携集合,由 make_portable.ps1 生成)
#         + deploy/bootstrap.ini([Integration] iniLocation=1 引导)
#      ->  ISCC 压成 dist/Gaze_<ver>_Setup.exe
# 安装语义: 装进 Program Files\Gaze(管理员一次到位);exe 旁只留引导 ini,
#           用户配置/缓存落 %APPDATA%(卸载保留,与便携版 exe 相对存放分流)。
# ═══════════════════════════════════════════════════════
param([string]$Version = "")

$ErrorActionPreference = "Stop"
$root  = Split-Path $PSScriptRoot -Parent
$dist  = Join-Path $root "dist"
$base  = Join-Path $dist "GazePortable"

# 版本号单一来源 = src/constants.h 的 GAZE_VERSION,免去打包版本与源码版本两处手改
if ([string]::IsNullOrWhiteSpace($Version)) {
    $verMatch = Select-String -Path (Join-Path $root "src/constants.h") `
                              -Pattern 'GAZE_VERSION[ ]*=[ ]*"([0-9]+\.[0-9]+\.[0-9]+)"' |
                Select-Object -First 1
    if (!$verMatch) { Write-Error "未能从 src/constants.h 解析 GAZE_VERSION(X.Y.Z)" }
    $Version = $verMatch.Matches[0].Groups[1].Value
}

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
# 成败以"产物在不在"为准:$LASTEXITCODE 在本机某些非交互宿主里取不到值(会误报
# "exit "),而 ISCC 其实已经成功 —— 曾据此误判过失败。产物才是硬事实。
if (!(Test-Path $setup)) { Write-Error "ISCC 未产出 $setup(exit $LASTEXITCODE),检查 gaze.iss" }
$sz = [math]::Round((Get-Item $setup).Length / 1MB, 1)
Write-Host "Installer OK: $setup ($sz MB)"
