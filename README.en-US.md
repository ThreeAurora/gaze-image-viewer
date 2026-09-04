<div align="center">

<img src="src/Gaze.png" alt="Gaze" width="128"/>

# Gaze

**A local media viewer & file browser for Windows**

简体中文 | [English](./README.en-US.md)

![Version](https://img.shields.io/badge/version-1.0.0-blue?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Windows-0078D6?style=flat-square&logo=windows&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6.8%20LTS-41CD52?style=flat-square&logo=qt&logoColor=white)
![License](https://img.shields.io/badge/license-private-red?style=flat-square)

🔥 Modeled after XnView MP, rebuilt on a modern stack (C++17 / Qt 6.8 LTS) — single process, fully portable, format-coverage first.

</div>

---

## 📑 Features

<details>
<summary><b>Click to expand all features</b> (~250 items, full table in <a href="FEATURES.en-US.md">FEATURES.en-US.md</a>)</summary>

**🖼 Browser**
- ✅ Three-pane layout: folder tree / file grid / preview panel, each toggleable, layout fully remembered
- ✅ 8 view modes: thumbnails, thumbnails+filename, +labels, details, icons, list, details table, waterfall
- ✅ 4-in-1 folder thumbnails: folders aggregate 4 preview images, fetched at high resolution then downsampled
- ✅ Persistent multi-tabs with tab thumbnails; Ctrl+W close, Ctrl+Shift+T restore, middle-click/double-click close
- ✅ Virtualized owner-drawn file grid: smooth scrolling in 100k-file folders, fixed 7-column header
- ✅ Thumbnail engine: background multithreaded + SQLite cache, 384/768/custom sizes
- ✅ Inline search (Ctrl+F type-to-search) + folder search dialog (include/exclude regex)

**🔍 Sort · Filter · Labels**
- ✅ 16-column header sorting: name/size/type/extension/created/modified/EXIF dual dates/dimensions/ratio/print size…
- ✅ Natural sorting (1, 2, … 10, not 1, 10, 2)
- ✅ 18 filter modes: images/videos/audio/documents/executables/folders/custom extension sets
- ✅ Color labels Ctrl+1~5: stored in SQLite, kept across sessions
- ✅ Filename color editor (extension → background color)

**👁 Viewer**
- ✅ Enter to enter, ESC to return — tree and grid hide, image fills the pane
- ✅ 1:1 pixel view (long-press), cursor-centered zoom, navigator mini-map with draggable blue frame
- ✅ GIF frame stepping / back-scrubbing / loop rewind
- ✅ Motion Photo: click the preview to play, XMP/ftyp dual-protocol detection
- ✅ PDF (Ghostscript) and text preview (auto truncation, word-wrap toggle, MD rendering)
- ✅ Metadata panel + histogram + EXIF overview
- ✅ Multi-image print layouts

**🎬 Video & Audio**
- ✅ 28 containers: MP4/MKV/WebM/FLV/RMVB/MXF…
- ✅ AV1 (bundled libdav1d, faulty hardware decoders refused), H.264/H.265, VP9 10-bit HDR10
- ✅ Playback bar: click-to-seek, remaining-time toggle, volume readout, left-click play/pause
- ✅ Fullscreen filmstrip gallery: whole folder included, image/video/audio filter buttons
- ✅ Audio waveform preview: decoded on a background thread, never blocks browsing
- ✅ Delete/move/rename while playing automatically releases the file

**🗃 Format & codec support (charter: all formats)**
- ✅ 33 image extensions: JPEG/PNG/GIF/WebP/BMP/TGA/TIFF/SVG/ICO/DDS/EXR/QOI/JPEG 2000…
- ✅ Modern formats: AVIF, HEIF (HEIC/HIF via bundled FFmpeg), JPEG XL
- ✅ RAW: 26 vendor extensions, LibRaw 0.21.4 statically linked, "Load original RAW" button
- ✅ CMYK JPEG: unified print-intent rendering + color interpretation toggle
- ✅ All codec components ship with the program (FFmpeg/Ghostscript/jpegtran) — zero system extensions required

**📂 File management**
- ✅ Delete to Recycle Bin (folders & batches included), F3 open with default app, F2/double-click rename
- ✅ Lossless JPEG rotate/crop (jpegtran)
- ✅ Drag & drop between tree and grid with clear forbidden-target feedback
- ✅ Single instance: launching again raises the existing window

**⚙️ Settings & integration**
- ✅ 20 settings pages, ~166 setting keys
- ✅ Dark/light theme switching live (no restart)
- ✅ Bilingual UI (Chinese/English, 828 strings fully translated)
- ✅ Explorer context menu "Browse with Gaze", file association registration, ms-settings shortcuts
- ✅ Search images by text: local CLIP+OCR semantic retrieval service
- ✅ Folder size computation (accurate background recursion + cache DB)
- ✅ Database maintenance page, crash minidump + event log self-diagnostics

</details>

---

## 📸 Screenshots

**Browser (three-pane layout)**

![Browser](docs/images/screenshot_browser.png)

**Viewer (Enter to enter, image fills the pane)**

![Viewer](docs/images/screenshot_viewer.png)

---

## 🖼 Supported formats

| Category | Coverage |
|---|---|
| Images | 33 extensions: JPEG / PNG / GIF / WebP / BMP / TGA / TIFF / SVG / ICO / DDS / EXR / QOI / JPEG 2000 … |
| Modern | AVIF, HEIF, JPEG XL (JXL), animated WebP |
| Professional | RAW (26 vendor extensions, LibRaw statically linked), CMYK JPEG |
| Video | 28 containers (MP4/MKV/WebM/FLV/RMVB/MXF…), H.264/H.265/AV1 (bundled libdav1d), VP9 10-bit HDR10 |
| Audio | 10 extensions, waveform preview (decoded on a background thread, never blocking browsing) |
| Documents | PDF (Ghostscript), TXT/MD text preview |

---

## 🥣 Usage

### Portable (recommended)

Download `Gaze_1.0.0_Portable.zip` from [Releases](../../releases), extract anywhere, and run `Gaze.exe`.

- All settings are stored in `Gaze.ini` inside the program folder, with the thumbnail cache in `thumbnails.db`
- No registry writes (only optional first-run shell integration); migration is simply copying the folder

### Installer

Download `Gaze_1.0.0_Setup.exe` from [Releases](../../releases) and follow the wizard. On first launch you can choose to move the configuration to `%APPDATA%` (the program folder stays read-only and user config survives uninstall).

### Build from source

```
Dependencies: CMake ≥ 3.16, Qt 6.8.3 (win64_mingw), MinGW 13.1.0 (SEH)
External libraries: ffmpeg / Ghostscript / jpegtran are bundled with the repo under vendor/; LibRaw sits under thirdparty/
```

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The build script automatically syncs `assets/` and `vendor/` into the build directory, so the output runs as-is.

---

## ⌨️ Keyboard shortcuts (selected)

| Key | Action |
|---|---|
| Enter / double-click | Enter the viewer (browser ↔ viewer toggle) |
| ESC | Return to the browser |
| G | Fullscreen preview (image only; layout restored exactly on exit) |
| F11 | Fullscreen window |
| Space | Default action / play-pause |
| Ctrl+F | Inline search |
| F2 / F3 | Rename / open with default app |
| Ctrl+W | Close the current tab |
| Ctrl+PgUp / PgDn | Switch tabs (progress seek on media pages) |
| Ctrl+1~5 | Color labels |
| B / F | Browse history back / forward |
| Home / End | First / last item |
| Long-press left button | 1:1 pixel view (cursor-focused) |

Full table in [FEATURES.en-US.md §11](FEATURES.en-US.md).

---

## 📜 Notes

- **Build artifacts are committed** (`build_qt68/Gaze.exe`) for direct verification
- **Development ledger**: [`todo.md`](todo.md) is the single ledger for task specs and progress; the full feature table lives in [`FEATURES.en-US.md`](FEATURES.en-US.md); per-key settings status in [`SETTINGS_MATRIX.md`](SETTINGS_MATRIX.md)
- ⚠️ **Before going public (internal)**: `src/assets/` contains 144 icons extracted from XnView — they must be fully replaced before any public/open-source release

---

## ♥️ Acknowledgements

- [Qt](https://www.qt.io/) — application framework
- [LibRaw](https://www.libraw.org/) — RAW decoding
- [FFmpeg](https://ffmpeg.org/) — audio/video decoding
- [Ghostscript](https://ghostscript.com/) — PDF rendering
- [jpegtran](https://jpegclub.org/) — lossless JPEG operations
- [XnView MP](https://www.xnview.com/en/xnviewmp/) — design reference for the interface and interactions

> This project is inspired by **XnView MP** and aims to serve as a lightweight alternative.
> Hats off to the original author, **Pierre-e Gougelet**!

---

## ⚠️ Disclaimer

This project is a personal-use tool intended for learning and exchange purposes only. Users are responsible for complying with the laws of their environment; the author bears no liability for any issues arising from the use of this software.

## 📄 License

Private project, not yet licensed for public distribution. Please contact the author if you wish to use it.
