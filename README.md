<div align="center">

<img src="src/Gaze.png" alt="Gaze" width="128"/>

# Gaze

**为 Windows 打造的本地媒体查看器与文件浏览器**

[English](./README.en-US.md) | 简体中文

![Version](https://img.shields.io/badge/version-1.2.0-blue?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Windows-0078D6?style=flat-square&logo=windows&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6.8%20LTS-41CD52?style=flat-square&logo=qt&logoColor=white)
![License](https://img.shields.io/badge/license-GPL--3.0-blue?style=flat-square)

### 🔥 一个程序，把浏览器 + 查看器 + 播放器 + 管理器的活全部干完

**全格式**（33 图片 · 26 RAW · 28 视频 · 10 音频 · PDF/TXT/MD）· **8K HDR10 实时播放** · **AV1 满速软解** · **Everything 秒级全盘搜** · **本地 CLIP 以文搜图** · **十万级目录流畅滚动** · 约 250 项功能 · 166 个设置键

### **无需额外建立文件库，解压即用、直接打开就能用；支持管理所有格式的文件。**

> 本项目受 **XnView MP** 启发，旨在作为一个轻量级的替代方案。
> 致敬原作者 **Pierre-e Gougelet**！

</div>

---

## ✨ 先看同类没有的

| | Gaze | 常见同类 |
|---|---|---|
| 📂 文件库 | **零文件库**：打开就是全盘，目录即所见，不等扫描 | 先建库 / 导入，等它索引完 |
| 📦 安装 | **解压即用**：不写注册表，拷目录即迁移，另备安装版 | 安装器 + 注册表 + 配置散落各处 |
| 🧩 解码器 | **FFmpeg / Ghostscript / LibRaw / dav1d 全部随包**，裸系统直开 AVIF / HEIC / RAW / PDF | 要求安装系统编解码器或扩展 |
| 🗂 格式管理 | **所有格式的文件都能浏览与管理**，预览另列清单 | 只认图片，其余类型视而不见 |
| 🔎 全盘搜索 | **Everything 引擎联动**：文件夹大小瞬间出精确值，全盘文件秒搜 | 只能在当前文件夹里翻 |
| 🧠 以文搜图 | **本地 CLIP+OCR 语义检索**：一句话找出那张图，数据不出本机 | 没有这个能力 |
| 🎬 视频 | 8K HDR10 实时播放 / AV1 满速软解 / 动态照片单击即播 | 常需外部播放器接力 |
| ⚡ 性能 | **缩略图生成极致优化**（后台多线程 + 缓存命中即出）· **视频进度条定位精准即时** | 缩略图干等，进度条拖动漂移 |
| 🖨 CMYK 印刷 | **CMYK JPEG 色彩管理解码** + 印刷口径一键切换 | 不支持，或解出来偏色 |

---


> 从文件切换、缩略图生成到标记与删除、分类筛选 —— 全文件管理与预览体验的每一条路径，都按性能极尽优化。

## 📑 项目功能

<details>
<summary><b>此处仅简略列出约 250 项功能</b> —— 完整功能清单见 <a href="FEATURES.md">FEATURES.md</a></summary>

| | |
|---|---|
| 🖼 **浏览与视图** | 三栏布局（树 / 网格 / 预览，六类面板独立开关记忆）· 8 种查看方式（缩略图 / +文件名 / +标签 / 详细信息 / 图标 / 列表 / 详细表 / 瀑布流）· 布局方案存档 · 常驻多标签页（缩略图标签，关后可恢复）· 后退 / 前进自动定位 · 地址栏（路径历史 / file:/// 兼容）· 文件树双向同步 · 隐藏文件着色 · 拖放移动 / 复制 · 标题模板 · 带文件启动五档 · 单实例唤起 |
| 🗂 **缩略图** | 后台多线程生成、缓存命中即出 · 四合一文件夹缩略图 · 三级解码管线（Qt → Shell → ffmpeg 兜底）· 单封面模式 · 抽帧位置可配 · HDR tone-mapping · 48–1024px 自定义 · SQLite 缓存与维护工具 |
| 👁 **查看器与预览** | 八类预览（图 / GIF / 视频 / 音频 / TXT / MD / PDF / RAW）· 1:1 像素查看 · 光标中心缩放 · 导航小图定位 · Gamma / 锐化 / HiDPI 1px=1px · 三层全屏（退出精确还原布局）· 顶部画廊 · Markdown 渲染 · PDF 渲染 · RAW 后台全解 · EXIF 分组树 + 直方图 · 文件夹大小秒出 · 视频闪帧根治 |
| 🎬 **视频与音频** | 28 种容器 · 8K HDR10 实时 · AV1 满速软解 · 进度条定位精准即时 · 动态照片单击即播 · GIF 双向擦洗 · 音频波形渐进绘制 · 快进快退步长可设 · 播放中删除 / 移动不占用文件 |
| 🔎 **搜索与排序** | Everything 联动秒搜全盘 · 本地 CLIP 以文搜图 · 16 列排序 · 自然排序 · 启动排序 10 档 · 18 种筛选 · 五色标记 · Ctrl+F 即搜 · 目录正则搜索（分时不冻界面） |
| 📋 **文件管理** | 约 25 项右键菜单 · 回收站删除（文件夹 / 批量同权）· JPEG 无损旋转 / 翻转 / 裁剪 · 非 JPEG 无损变换 · 多图排版打印（15 项全持久化）· EXIF 方向自动旋转 · 重命名焦点路由 · 最近文件管理 |
| 🗃 **格式与解码** | AVIF / JXL → ffmpeg（dav1d / libjxl）· HEIC → WIC · CMYK 色彩管理解码 + 印刷口径切换 · 文件头嗅探 · RAW 独立白名单（LibRaw 静态编入）· 全部解码依赖随包 |
| ⚙️ **设置与集成** | 数百个设置项 · 20 分类页 · 快捷键全可配（可视化编辑）· 深 / 浅双主题 · 文件名底色编辑器 · 资源管理器右键集成 · 文件关联注册 · 便携 / %APPDATA% 迁移 |
| 🛡 **稳定性与诊断** | GUI 心跳看门狗 · 崩溃 minidump · 启动检查点链 · 全级别日志自诊 |

</details>

---

## 📸 程序截图

**浏览器（三栏布局 · 支持多格式预览）**

![浏览器](docs/images/screenshot_browser.webp)

**查看器（多标签 · 多语言展示）**

![查看器](docs/images/screenshot_viewer_en.webp)

**全屏预览（顶部画廊）**

![全屏预览](docs/images/screenshot_gfull.webp)

**设置（数百个设置项，细到每一处行为）**

![设置](docs/images/screenshot_settings.webp)

**快捷键（真正好用的那一批）**

![快捷键](docs/images/screenshot_shortcuts.webp)

---

## 🖼 支持预览的格式

> **所有格式的文件都能浏览与管理**；下表列出的是支持**预览**（看图 / 播放 / 文档渲染）的格式。

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

从 [Releases](../../releases) 下载 `Gaze_1.2.0_Portable.zip`，解压到任意位置，双击 `Gaze.exe` 运行。

- 所有设置保存在程序目录内的 `Gaze.ini`，缩略图缓存在 `thumbnails.db`
- **无需导入、无需建立文件库**：目录即所见，打开就是浏览器
- 不写注册表（仅首次可选的系统集成项），拷贝目录即完成迁移

### 安装版

从 [Releases](../../releases) 下载 `Gaze_1.2.0_Setup.exe`，按向导安装。首次启动可选择把配置迁移到 `%APPDATA%`（程序目录只读、卸载保留用户配置）。

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
| C / V | 上一个 / 下一个文件 |
| 空格 | 播放 / 暂停 |
| F | 颜色标记 |
| D | 取消标记 |
| Ctrl+1~5 | 五色标记直选 |
| X | 新建文件夹 |
| S | 删除（进回收站） |
| 右键 + 滚轮 | 调整进度条 |
| 长按左键 | 光标处缩放 |
| Ctrl+滚轮 | 缩放画面 |
| Enter / 双击 | 进入查看器 |
| ESC | 返回浏览器 |
| G | 全屏预览（退出精确还原布局） |
| F11 | 界面全屏 |
| Ctrl+F | 内联搜索 |
| Ctrl+W | 关闭当前标签页 |
| Ctrl+PgUp / PgDn | 切换标签页 |

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
- [XnView MP](https://www.xnview.com/en/xnviewmp/) —— 界面形态与交互的设计参考

---

## ⚠️ 免责声明

本项目为个人自用工具，仅供学习与交流使用。使用者请自行确保遵守所运行环境的相关法律法规，因使用本软件产生的任何问题由使用者自行承担。

## 📄 License

本项目以 [GPL-3.0](./LICENSE) 许可发布。图标资产来自 Material Design Icons 与 Lucide（宽松许可，可再分发），部分为自绘。
