<div align="center">

<img src="src/Gaze.png" alt="Gaze" width="128"/>

# Gaze

**一款为 Windows 打造的本地媒体查看器与文件浏览器**

[English](./README.en-US.md) | 简体中文

![Version](https://img.shields.io/badge/version-1.0.0-blue?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Windows-0078D6?style=flat-square&logo=windows&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6.8%20LTS-41CD52?style=flat-square&logo=qt&logoColor=white)
![License](https://img.shields.io/badge/license-GPL--3.0-blue?style=flat-square)

🔥 受 XnView MP 启发的轻量级开源替代方案，以现代化技术栈（C++17 / Qt 6.8 LTS）重新实现 —— 高性能、高舒适度、格式广度优先。

</div>

> 灵感源自 **XnView MP**。本项目尝试做一个轻量级的开源替代：当您想按自己的习惯定制看图工具时，这里提供更多的可能。
> 致敬原作者 **Pierre-e Gougelet**！

---

## ✨ 亮点速览

- **格式广度优先** —— 33 种图片扩展名 + 26 种 RAW（LibRaw 静态编入）+ AVIF / HEIC / JPEG XL；28 种视频容器连 RMVB、MXF 都认；解码组件全随程序分发，零系统依赖
- **大目录也流畅** —— 虚拟化自绘文件网格 + 后台多线程缩略图引擎 + SQLite 缓存，十万级文件目录依旧滚动如丝
- **看照片的方式很讲究** —— 四合一文件夹缩略图、8 种查看方式（瀑布流 / 详细信息表…）、1:1 像素长按查看、光标中心缩放
- **动起来也拿手** —— 动态照片（Motion Photo）单击即播、GIF 逐帧步进、全屏胶卷画廊一页看尽整个目录
- **整理不费劲** —— Ctrl+1~5 颜色标记、18 种筛选模式、16 列排序、以文搜图（本地 CLIP + OCR 语义检索，不出本机）
- **细节控狂喜** —— CMYK 印刷口径渲染、JPEG 无损旋转裁剪、音频波形预览、PDF 直读、直方图与 EXIF 面板
- **随身携带** —— 单目录便携、不写注册表；深浅双主题即点即换；中英双语 828 条全量翻译

---

## 📑 项目功能

<details>
<summary><b>点击展开全部特性</b>（约 250 项，逐项全表见 <a href="FEATURES.md">FEATURES.md</a>）</summary>

**🖼 浏览器**
- ✅ 三栏布局：目录树 / 文件网格 / 预览面板，各自可开关、布局全程记忆
- ✅ 8 种查看方式：缩略图、缩略图+文件名、+标签、详细信息、图标、列表、详细信息表、瀑布流
- ✅ 四合一文件夹缩略图：文件夹聚合其内 4 张预览图，向原图取高清再降采样
- ✅ 常驻多标签页 + 标签缩略图，Ctrl+W 关签、Ctrl+Shift+T 恢复、中键/双击关签
- ✅ 虚拟化自绘文件网格：十万级文件目录依旧流畅滚动，表头 7 列固定
- ✅ 缩略图引擎：后台多线程 + SQLite 缓存，尺寸 384/768/自定义
- ✅ 内联搜索（Ctrl+F 输入即搜）+ 目录搜索对话框（含/排除正则）

**🔍 排序 · 筛选 · 标记**
- ✅ 表头 16 列排序：名称/大小/类型/扩展名/创建/修改/EXIF 双日期/尺寸/比例/打印尺寸…
- ✅ 自然排序（1, 2, … 10，不是 1, 10, 2）
- ✅ 18 种筛选模式：图片/视频/音频/文档/可执行/文件夹/自定义扩展名集合
- ✅ 颜色标记 Ctrl+1~5：SQLite 存储，跨会话保留
- ✅ 文件名底色编辑器（扩展名 → 底色）

**👁 查看器**
- ✅ Enter 进入、ESC 返回，树与网格隐藏画面占满
- ✅ 1:1 像素查看（长按）、光标中心缩放、导航小图蓝框拖动定位
- ✅ GIF 逐帧步进 / 跳帧回拖 / 片尾回卷
- ✅ 动态照片（Motion Photo）单击即播，XMP/ftyp 双协议探测
- ✅ PDF（Ghostscript 渲染）与文本预览（自动截断、换行开关、MD 渲染）
- ✅ 元数据面板 + 直方图 + EXIF 一览
- ✅ 多图排版打印

**🎬 视频与音频**
- ✅ 28 种容器播放：MP4/MKV/WebM/FLV/RMVB/MXF…
- ✅ AV1（内置 libdav1d，拒绝故障硬解设备）、H.264/H.265、VP9 10bit HDR10
- ✅ 播放控制栏：点击跳转、剩余时间切换、音量数值、左键播放/暂停
- ✅ 全屏胶卷画廊：全目录参与、图片/视频/音频分类过滤钮
- ✅ 音频波形预览：后台线程解码，绝不阻塞浏览
- ✅ 播放中删除/移动/重命名自动解除文件占用

**🗃 格式与解码（首要要求：全格式）**
- ✅ 33 种图片扩展名：JPEG/PNG/GIF/WebP/BMP/TGA/TIFF/SVG/ICO/DDS/EXR/QOI/JPEG 2000…
- ✅ 现代格式：AVIF、HEIF（HEIC/HIF 走随包 FFmpeg）、JPEG XL
- ✅ RAW：26 种厂商扩展名，LibRaw 0.21.4 静态编入，「加载原始RAW」按钮
- ✅ CMYK JPEG：印刷口径统一渲染 + 颜色解释切换
- ✅ 解码组件全部随程序分发（FFmpeg/Ghostscript/jpegtran），不要求安装任何系统扩展

**📂 文件管理**
- ✅ 删除走回收站（含文件夹与批量）、F3 用默认应用打开、F2/双击重命名
- ✅ JPEG 无损旋转/裁剪（jpegtran）
- ✅ 拖放：文件树/网格互拖移动，禁止落点明确反馈
- ✅ 单实例运行：再次启动唤起已有窗口

**⚙️ 设置与集成**
- ✅ 20 个分类设置页、约 166 个设置键
- ✅ 深/浅双主题即时切换（无需重启）
- ✅ 中英双语界面（828 条全量翻译）
- ✅ 资源管理器右键「用 Gaze 浏览」、文件关联注册、ms-settings 跳转
- ✅ 以文搜图：接入本地 CLIP+OCR 语义检索服务
- ✅ 文件夹大小统计（后台精确递归 + 缓存库）
- ✅ 数据库维护页、崩溃 minidump + 事件日志自诊

</details>

---

## 📸 程序截图

| 浏览器 · 三栏布局 | 查看器 · Enter 画面占满 |
|:---:|:---:|
| ![浏览器](docs/images/screenshot_browser.png) | ![查看器](docs/images/screenshot_viewer.png) |

*深色主题 · 标准测试图库下的实际运行画面，所有界面元素均为程序实时渲染。*

---

## 🖼 支持格式

| 类别 | 覆盖 |
|---|---|
| 图片 | JPEG / PNG / GIF / WebP / BMP / TGA / TIFF / SVG / ICO / DDS / EXR / QOI / JPEG 2000 等共 33 种扩展名 |
| 现代格式 | AVIF、HEIF、JPEG XL（JXL）、WebP 动图 |
| 专业格式 | RAW（26 种厂商扩展名，内置 LibRaw 静态编入）、CMYK JPEG |
| 视频 | 28 种容器（MP4/MKV/WebM/FLV/RMVB/MXF…），H.264/H.265/AV1（内置 libdav1d）、VP9 10bit HDR10 |
| 音频 | 10 种扩展，波形预览（后台线程解码，绝不阻塞浏览） |
| 文档 | PDF（Ghostscript）、TXT/MD 文本预览 |

---

## 🥣 使用方法

### 便携版（推荐）

从 [Releases](../../releases) 下载 `Gaze_1.0.0_Portable.zip`，解压到任意位置，双击 `Gaze.exe` 运行。

- 所有设置保存在程序目录内的 `Gaze.ini`，缩略图缓存在 `thumbnails.db`
- 不写注册表（仅首次可选的系统集成项），拷贝目录即完成迁移

### 安装版

从 [Releases](../../releases) 下载 `Gaze_1.0.0_Setup.exe`，按向导安装。首次启动可选择把配置迁移到 `%APPDATA%`（程序目录只读、卸载保留用户配置）。

### 从源码构建

```
依赖：CMake ≥ 3.16、Qt 6.8.3 (win64_mingw)、MinGW 13.1.0 (SEH)
外部库：ffmpeg / Ghostscript / jpegtran 请从官方渠道获取并放置到构建期望的目录；LibRaw 位于 thirdparty/
```

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

图标资源由 `tools/fetch_mdi_icons.py` / `tools/fetch_lucide_icons.py` 从上游下载渲染（构建前运行即可再生成），构建脚本会自动同步资源到构建目录，产物直接可运行。

---

## ⌨️ 快捷键（精选）

| 键 | 作用 |
|---|---|
| Enter / 双击 | 进入查看器（浏览器 ↔ 查看器切换） |
| ESC | 返回浏览器 |
| G | 全屏预览（仅画面，退出后精确还原布局） |
| F11 | 界面全屏 |
| 空格 | 默认操作 / 播放暂停 |
| Ctrl+F | 内联搜索 |
| F2 / F3 | 重命名 / 用默认应用打开 |
| Ctrl+W | 关闭当前标签页 |
| Ctrl+PgUp / PgDn | 切换标签页（视频页=进度调整） |
| Ctrl+1~5 | 颜色标记 |
| B / F | 目录浏览历史后退 / 前进 |
| Home / End | 首个 / 末个条目 |
| 长按左键 | 1:1 像素查看（光标聚焦） |

全表见 [FEATURES.md §十一](FEATURES.md)。

---

## 📜 其他说明

- **图标资产**：应用图标来自 [Material Design Icons](https://materialdesignicons.com/) 与 [Lucide](https://lucide.dev/)（均为宽松许可、可再分发），部分为自绘；由 `tools/` 下脚本下载渲染，构建前运行即可再生成
- **构建产物不入库**：`build_qt68/` 等编译产物与第三方运行库不随仓库分发，按上述流程自行构建

---

## ♥️ 致谢

- [Qt](https://www.qt.io/) —— 应用框架
- [LibRaw](https://www.libraw.org/) —— RAW 解码
- [FFmpeg](https://ffmpeg.org/) —— 音视频解码
- [Ghostscript](https://ghostscript.com/) —— PDF 渲染
- [jpegtran](https://jpegclub.org/) —— JPEG 无损操作

---

## ⚠️ 免责声明

本项目为个人自用工具，仅供学习与交流使用。使用者请自行确保遵守所运行环境的相关法律法规，因使用本软件产生的任何问题由使用者自行承担。

## 📄 License

本项目以 [GPL-3.0](./LICENSE) 许可发布。图标资产来自 Material Design Icons 与 Lucide（宽松许可，可再分发），部分为自绘。
