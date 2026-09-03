# ═══════════════════════════════════════════════════════
# 安装版 Setup.exe 打包:用 Windows 自带 IExpress 做自解压安装器。
#   用法: powershell -ExecutionPolicy Bypass -File deploy/make_installer.ps1 [-Version 1.0]
#
# 产物链:  dist/GazePortable(便携集合,由 make_portable.ps1 生成)
#         + deploy/install.cmd + uninstall.cmd
#      ->  dist/sfx(净后备区,含脚手架) -> IExpress 压成 dist/Gaze_<ver>_Setup.exe
# 安装语义: 解压到临时目录 -> 自动跑 install.cmd -> 装进 %LOCALAPPDATA%\Programs\Gaze
#           (免管理员;配置/缓存随 exe 相对存放,与便携版同口径)。
# ═══════════════════════════════════════════════════════
param([string]$Version = "1.0")
if ([string]::IsNullOrWhiteSpace($Version)) { $Version = "1.0" }

$ErrorActionPreference = "Stop"
$root  = Split-Path $PSScriptRoot -Parent
$dist  = Join-Path $root "dist"
$base  = Join-Path $dist "GazePortable"
$sfx   = Join-Path $dist "sfx_stack"
$setup = Join-Path $dist "Gaze_${Version}_Setup.exe"

if (!(Test-Path (Join-Path $base "Gaze.exe"))) {
    Write-Host "dist\GazePortable 不存在,先跑 deploy/make_portable.ps1"
    exit 1
}

# 1) 净后备区 = 便携集合 + 安装/卸载脚手架
if (Test-Path $sfx) { Remove-Item -Recurse -Force $sfx }
New-Item -ItemType Directory -Force -Path $sfx | Out-Null
Copy-Item -Recurse -Force (Join-Path $base "*") $sfx
Copy-Item -Force (Join-Path $PSScriptRoot "install.cmd")   $sfx
Copy-Item -Force (Join-Path $PSScriptRoot "uninstall.cmd") $sfx

# 2) 生成 IExpress .SED(文件清单按实导出,免手写几百行)
$files = Get-ChildItem $sfx -Recurse -File
$sedLine = [System.Collections.Generic.List[string]]::new()
$sedLine.Add("[Version]")
$sedLine.Add("Class=iexpress")
$sedLine.Add("SEDVersion=3")
$sedLine.Add("[Options]")
$sedLine.Add("PackagePurpose=InstallApp")
$sedLine.Add("ShowContractLicense=No")
$sedLine.Add("ShowProgress=No")          # 不弹"解压到哪"的选择框,直接跑安装命令
$sedLine.Add("UsePackageTargetDir=False")
$sedLine.Add("InstallPrompt=Gaze $Version 安装程序:将安装到用户目录(%LOCALAPPDATA%\Programs\Gaze)并创建快捷方式。继续?")
$sedLine.Add("InstallCommand=cmd.exe /c install.cmd")
$sedLine.Add("SEDIT=" + (Join-Path $sfx "i386"))
$sedLine.Add("ExtractOrder=1")
$sedLine.Add("[Strings]")
$sedLine.Add("CustomSetupOnly=")
$sedLine.Add("[SourceFiles]")
$sedLine.Add("SourceFiles0=" + $sfx)
$sedLine.Add("[SourceFiles0]")
$i = 0
foreach ($f in $files) {
    $rel = $f.FullName.Substring($sfx.Length).TrimStart('\', '/')
    $sedLine.Add("%FILE$i%=" + $rel)
    $i++
}
$sedPath = Join-Path $dist "setup.sed"
Set-Content -Path $sedPath -Value $sedLine -Encoding Ascii

# 3) IExpress 非交互打包
if (Test-Path $setup) { Remove-Item -Force $setup }
& iexpress /Q $sedPath | Out-Null
if (!(Test-Path $setup)) { Write-Error "IExpress 未产出 $setup,检查 setup.sed" }
$sz = [math]::Round((Get-Item $setup).Length / 1MB, 1)
Write-Host "Installer OK: $setup ($sz MB)  [SED: $sedPath]"