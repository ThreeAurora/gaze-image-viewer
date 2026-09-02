# Gaze

**一款为 Windows 打造的本地媒体查看器与文件浏览器** —— 图片、视频、RAW、PDF，一个窗口全搞定。界面与交互对标 XnView MP，以现代化的技术栈（C++17 / Qt 6.8 LTS）重新实现。

- 单进程、便携式：解压即用，设置随程序目录保存
- 原生渲染：文件网格为虚拟化自绘控件，十万级文件目录依旧流畅滚动
- 格式广度优先：从主流 JPEG/PNG 到 AVIF / JXL / RAW / AV1 / HDR，能解就显示

> 本项目受 **XnView MP** 启发，旨在作为一个轻量级的开源替代方案。
> 致敬原作者 **Pierre-e Gougelet**！

---

## ✨ 特性

### 浏览器
- **三栏布局**：目录树 / 文件网格 / 预览面板，各自可开关、可记忆布局
- **8 种查看方式**：缩略图、缩略图+文件名、+标签、详细信息、图标、列表、详细信息表、瀑布流；列数 1–16 固定，缩放贴边不换列
- **四合一文件夹缩略图**：文件夹图标聚合其内 4 张预览图，向原图取高清再降采样
- **多标签页**：常驻浏览器标签，Ctrl+W 关闭；地址栏单击全选、回车直达、粘贴文件路径定位
- **排序与筛选**：表头 7 列排序（含 EXIF 日期、创建日期）、自然排序（1, 2, … 10）、按格式/自定义扩展名筛选
- **颜色标记**：SQLite 存储，Ctrl+1~5 快速标色，跨会话保留
- **内联搜索**：Ctrl+F 输入即搜，上一个/下一个/高亮

### 查看器
- **回车进入，ESC 返回**：树与网格隐藏，画面占满
- **缩放与定位**：1:1 像素级查看（长按）、光标中心缩放、Ctrl+滚轮步进、导航小图蓝框拖动
- **视频播放**：播放/暂停/音量/进度（点击跳转、剩余时间切换），HDR（VP9 10bit HDR10）与 AV1 硬解兼容路径
- **动态照片（Motion Photo）**：单击预览即播，播完自动切回静态帧，XMP/ftyp 双协议探测
- **GIF 控制**：逐帧步进、跳帧回拖、片尾回卷
- **PDF 与文本预览**：Ghostscript 渲染；长文本自动截断
- **元数据面板与直方图**：EXIF 信息一览
- **打印**：多图排版打印布局

### 格式支持
| 类别 | 覆盖 |
|---|---|
| 图片 | JPEG / PNG / GIF / WebP / BMP / TGA / PCX / TIFF… |
| 现代格式 | AVIF、HEIF、JPEG XL（JXL）、WebP 动图 |
| 专业格式 | RAW（Cr2/Cr3 等多厂商，内置 LibRaw）、PSD、CMYK JPEG、EXR/HDR |
| 视频 | MP4/MKV/WebM…，H.264/H.265/AV1（内置 libdav1d，拒绝故障硬解设备）、VP9 10bit HDR10 |
| 音频 | 波形预览（后台线程解码，绝不阻塞浏览） |

### 工具与集成
- **无损操作**：JPEG 无损旋转 / 裁剪（jpegtran）
- **随程序分发**：ffmpeg、Ghostscript、jpegtran 全部内置于程序目录，不要求用户安装任何解码器或系统扩展
- **系统集成**：资源管理器右键「用 Gaze 浏览」、打开方式注册、单实例运行（再次启动唤起已有窗口）
- **以文搜图**：接入本地语义检索服务（CLIP + OCR + 文件名），自然语言搜图

---

## 📦 安装

**便携版（推荐）**：将整个程序目录放置任意位置，双击 `Gaze.exe` 运行。

- 所有设置保存在程序目录内的 `Gaze.ini`，缩略图缓存在 `thumbnails.db`
- 不写注册表（仅首次可选的系统集成项），拷贝目录即完成迁移

## 🔨 从源码构建

```
依赖：CMake ≥ 3.16、Qt 6.8.3 (win64_mingw)、MinGW 13.1.0 (SEH)
外部库：ffmpeg / Ghostscript / jpegtran 已随仓库 vendor/ 目录附带；LibRaw 位于 thirdparty/
```

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

构建脚本会自动同步 `assets/` 与 `vendor/` 到构建目录，产物直接可运行。

## ⌨️ 快捷键（精选）

| 键 | 作用 |
|---|---|
| Enter / 双击 | 进入查看器（浏览器 ↔ 查看器切换） |
| ESC | 返回浏览器 |
| G | 全屏预览（仅画面，退出后精确还原布局） |
| F11 | 界面全屏 |
| 空格 | 默认操作 / 播放 |
| Ctrl+F | 内联搜索 |
| F3 | 重命名 |
| Ctrl+W | 关闭当前标签页 |
| Ctrl+1~5 | 颜色标记 |
| 长按左键 | 1:1 像素查看（光标聚焦） |

## 🗂 项目结构

```
src/app/       应用壳：主窗口、设置、主题
src/views/     文件网格（虚拟化自绘）、目录树、预览面板、右键菜单
src/media/     缩略图引擎、动态照片解析、颜色标记库
src/dialogs/   设置页（20 个分类页）、打印、以文搜图、数据库维护
vendor/        随程序分发的外部工具（ffmpeg、Ghostscript、jpegtran）
thirdparty/    编入的第三方源码（LibRaw 等）
```

## 🧭 Roadmap

- [ ] 浅色主题打磨与运行时主题热切换
- [ ] HEIF / CMYK 解码全内置（去除对系统 WIC 扩展的依赖）
- [ ] 更多 RAW 厂商格式实测覆盖
- [ ] 以文搜图深度集成（目录管理与模型管理界面）

## 🙏 致谢

- [Qt](https://www.qt.io/) —— 应用框架
- [LibRaw](https://www.libraw.org/) —— RAW 解码
- [FFmpeg](https://ffmpeg.org/) —— 音视频解码
- [Ghostscript](https://ghostscript.com/) —— PDF 渲染
- [jpegtran](https://jpegclub.org/) —— JPEG 无损操作
- [XnView MP](https://www.xnview.com/en/xnviewmp/) —— 界面形态与交互的设计参考

## 📄 License

私有项目，暂未授权公开分发。如需使用请与作者联系。

---

### 📎 关于本仓库（内部说明）

本仓库同时入库了构建产物，便于直接取用验证。开发进度与任务规格的唯一账本是 [`todo.md`](todo.md)。
