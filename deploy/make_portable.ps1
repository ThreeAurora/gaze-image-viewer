# ═══════════════════════════════════════════════════════
# 便携版 Portable 打包:build_qt68 运行期集合 -> dist/GazePortable/ + zip
# 用法: powershell -ExecutionPolicy Bypass -File deploy/make_portable.ps1 [-Version 1.0]
#
# 打包口径(与安装版共用同一份"运行期集合"清单,见下方 $Excludes):
#   带什么: Gaze.exe + Qt/ffmpeg/msvcrt dll + plugins(imageformats/platforms/…)
#            + assets + gs + ffmpeg + jpegtran + gaze_en.qm
#   不带什么: 一切可再生/开发中间物(CMake、日志、崩溃转储、缩略图库、
#            Gaze.ini、探针 exe、librawdec.a、rel_avif.json)——
#            首次运行自动重建 Gaze.ini/thumbnails.db,保证干净首启。
# ═══════════════════════════════════════════════════════
param([string]$Version = "1.0")
if ([string]::IsNullOrWhiteSpace($Version)) { $Version = "1.0" }

$ErrorActionPreference = "Stop"
$root   = Split-Path $PSScriptRoot -Parent
$build  = Join-Path $root "build_qt68"
$dist   = Join-Path $root "dist"
$out    = Join-Path $dist "GazePortable"

if (!(Test-Path (Join-Path $build "Gaze.exe"))) {
    Write-Error "build_qt68 里没有 Gaze.exe,先编译再打包"
}

$Excludes = @(
    # 构建/生成物
    "CMakeCache.txt", "Makefile", "cmake_install.cmake",
    "Gaze_autogen", "rawdec_autogen", "CMakeFiles", ".qt",
    "librawdec.a",
    # 探针/工具 exe
    "cmyk_probe.exe", "rawtest.exe",
    # 日志/转储/缓存(首次运行自建)
    "gaze.log", "perf.log", "winhook.log", "gaze_crash.dmp",
    "thumbnails.db", "thumbnails.db-shm", "thumbnails.db-wal",
    "Gaze.ini",
    # 调研中间物(零引用)
    "rel_avif.json"
)

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force -Path $out | Out-Null

[Console]::WriteLine("DEBUG Excludes count=" + $Excludes.Count)   # 临时探针
[Console]::WriteLine("DEBUG Excludes content=" + ($Excludes -join '|'))

$files = Get-ChildItem $build -Force | Where-Object { $_.Name -notin $Excludes }
foreach ($f in $files) {
    Copy-Item -Recurse -Force $f.FullName (Join-Path $out $f.Name)
}

$zip = Join-Path $dist "Gaze_${Version}_Portable.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$out/*" -DestinationPath $zip -CompressionLevel Optimal

$sz = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "Portable OK: $zip ($sz MB)  [条目数: $((Get-ChildItem $out -Recurse -File).Count)]"