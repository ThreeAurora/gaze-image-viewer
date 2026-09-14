# ═══════════════════════════════════════════════════════
# 便携版 Portable 打包:build_qt68 运行期集合 -> dist/GazePortable/ + zip
# 用法: powershell -ExecutionPolicy Bypass -File deploy/make_portable.ps1 [-Version X.Y.Z]
#       不传 -Version 时,版本号取自 src/constants.h 的 GAZE_VERSION(单一来源)
#
# 打包口径(与安装版共用同一份"运行期集合"清单,见下方 $Excludes):
#   带什么: Gaze.exe + Qt/ffmpeg/msvcrt dll + plugins(imageformats/platforms/…)
#            + assets + gs + ffmpeg + jpegtran + gaze_en.qm
#   不带什么: 一切可再生/开发中间物(CMake、日志、崩溃转储、缩略图库、
#            Gaze.ini、探针 exe、librawdec.a、rel_avif.json)——
#            首次运行自动重建 Gaze.ini/thumbnails.db,保证干净首启。
# ═══════════════════════════════════════════════════════
param([string]$Version = "")

$ErrorActionPreference = "Stop"
$root   = Split-Path $PSScriptRoot -Parent

# 版本号单一来源 = src/constants.h 的 GAZE_VERSION,免去打包版本与源码版本两处手改
if ([string]::IsNullOrWhiteSpace($Version)) {
    $verMatch = Select-String -Path (Join-Path $root "src/constants.h") `
                              -Pattern 'GAZE_VERSION[ ]*=[ ]*"([0-9]+\.[0-9]+\.[0-9]+)"' |
                Select-Object -First 1
    if (!$verMatch) { Write-Error "未能从 src/constants.h 解析 GAZE_VERSION(X.Y.Z)" }
    $Version = $verMatch.Matches[0].Groups[1].Value
}

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
    "cmyk_probe.exe", "rawtest.exe", "hw_probe.exe",
    # 日志/转储/缓存(首次运行自建)
    "gaze.log", "gaze.log.old", "perf.log", "winhook.log", "gaze_crash.dmp",
    "hang.dmp", "mdmp.py", "mdmp_out.txt", "mdmp_threads.py", "dump_prog.py",
    "thumbnails.db", "thumbnails.db-shm", "thumbnails.db-wal",
    "Gaze.ini",
    # 调研中间物(零引用)
    "rel_avif.json"
)

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force -Path $out | Out-Null

$files = Get-ChildItem $build -Force | Where-Object { $_.Name -notin $Excludes }
foreach ($f in $files) {
    if ($f.Name -eq "everything") {
        # 引擎只带本体:Everything.exe/es.exe/语言/许可。绝不打包本机的
        # Everything-gaze.db / backup.db(全盘 NTFS 索引库,可 300MB+)与
        # 实例 ini/session —— 那是运行时状态,带走既是隐私也是体积炸弹。
        New-Item -ItemType Directory -Force -Path (Join-Path $out "everything") | Out-Null
        foreach ($keep in @("Everything.exe", "es.exe", "Everything.lng", "License.txt")) {
            $src = Join-Path $f.FullName $keep
            if (Test-Path $src) { Copy-Item -Force $src (Join-Path $out "everything") }
        }
    } elseif ($f.Name -eq "multimedia") {
        # Qt 的 ffmpeg 插件(ffmpegmediaplugin.dll)按 QLibrary 走朴素 LoadLibrary
        # (qtbase/src/corelib/plugin/qlibrary_win.cpp,Qt 6.8 实测),其 av*/sw*
        # 依赖从"应用程序目录"(本包根)解析 —— 根目录已带整套。windeployqt 在
        # multimedia/ 里又放了一份同名运行库,加载器根本不会看它:纯冗余(约 89MB,
        # 每版 zip 都白胖这么大)。这里只保留 Qt 自己的插件 dll,不搬 ffmpeg 运行库。
        New-Item -ItemType Directory -Force -Path (Join-Path $out "multimedia") | Out-Null
        Get-ChildItem $f.FullName -File |
            Where-Object { $_.Name -notmatch '^(avcodec|avformat|avutil|swresample|swscale)-' } |
            ForEach-Object { Copy-Item -Force $_.FullName (Join-Path $out "multimedia") }
    } else {
        Copy-Item -Recurse -Force $f.FullName (Join-Path $out $f.Name)
    }
}

# 硬校验:multimedia/ 里的 ffmpeg 运行库已剥离,根目录那一套就必须齐全,
# 否则视频播放会缺 avcodec。宁可打包失败,也不产出"能启动但放不了视频"的包。
# 用通配匹配,免得 ffmpeg 大版本号(avcodec-61…)一变就失效。
foreach ($pat in @("avcodec-*.dll", "avformat-*.dll", "avutil-*.dll",
                   "swresample-*.dll", "swscale-*.dll")) {
    if (!(Get-ChildItem (Join-Path $out $pat) -ErrorAction SilentlyContinue)) {
        Write-Error "打包缺根目录 $pat:ffmpeg 运行库不全,检查 build_qt68"
    }
}

$zip = Join-Path $dist "Gaze_${Version}_Portable.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$out/*" -DestinationPath $zip -CompressionLevel Optimal

$sz = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "Portable OK: $zip ($sz MB)  [条目数: $((Get-ChildItem $out -Recurse -File).Count)]"