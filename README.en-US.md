# Gaze

[中文](./README.md) | English

**A local media viewer and file browser built for Windows** — images, videos, RAW, and PDF, all handled in a single window. The interface and interactions are modeled on XnView MP, reimplemented on a modern tech stack (C++17 / Qt 6.8 LTS).

- Single-process and portable: unzip and run; settings are kept inside the program directory
- Native rendering: the file grid is a virtualized custom-drawn widget that keeps scrolling smoothly even in folders with 100k+ files
- Format breadth first: from mainstream JPEG/PNG to AVIF / JXL / RAW / AV1 / HDR — if it can be decoded, it gets displayed

> This project is inspired by **XnView MP** and aims to serve as a lightweight open-source alternative.
> Hats off to the original author, **Pierre-e Gougelet**!

---

## ✨ Features

### Browser
- **Three-pane layout**: directory tree / file grid / preview panel, each independently toggleable, with layouts remembered
- **8 view modes**: thumbnails, thumbnails + file names, + labels, details, icons, list, details table, and waterfall; column counts fixed at 1–16, zoom snaps to the edges without reflowing columns
- **Four-in-one folder thumbnails**: a folder icon aggregates 4 preview images from inside it, fetched at high resolution from the originals and then downsampled
- **Multi-tab**: persistent browser tabs, closed with Ctrl+W; click the address bar to select all, press Enter to jump, or paste a file path to locate it
- **Sort & filter**: 7 sortable columns (including EXIF date and creation date), natural sort (1, 2, … 10), filtering by format or custom extensions
- **Color labels**: stored in SQLite, quick labeling via Ctrl+1~5, preserved across sessions
- **Inline search**: press Ctrl+F and type to search instantly, with previous/next/highlight

### Viewer
- **Enter to view, ESC to return**: the tree and grid hide, and the image fills the window
- **Zoom & positioning**: 1:1 pixel-level viewing (long-press), cursor-centered zoom, stepped zoom via Ctrl+scroll wheel, dragging the blue frame on the navigator mini-map
- **Video playback**: play/pause/volume/progress (click to seek, remaining-time toggle), with an HDR (VP9 10-bit HDR10) path and an AV1 hardware-decode compatibility route
- **Motion photos**: a single click on the preview plays the motion, switching back to the still frame automatically when done; detected via the dual XMP/ftyp protocols
- **GIF controls**: frame-by-frame stepping, back-scrubbing by skipping frames, rewind at the end
- **PDF & text preview**: rendered with Ghostscript; long text is truncated automatically
- **Metadata panel & histogram**: EXIF info at a glance
- **Printing**: multi-image print layout

### Format support
| Category | Coverage |
|---|---|
| Images | JPEG / PNG / GIF / WebP / BMP / TGA / PCX / TIFF… |
| Modern formats | AVIF, HEIF, JPEG XL (JXL), animated WebP |
| Professional formats | RAW (Cr2/Cr3 and other vendors, with built-in LibRaw), PSD, CMYK JPEG, EXR/HDR |
| Video | MP4/MKV/WebM…, H.264/H.265/AV1 (built-in libdav1d, rejecting faulty hardware-decode devices), VP9 10-bit HDR10 |
| Audio | Waveform preview (decoded on a background thread, never blocking browsing) |

### Tools & integration
- **Lossless operations**: lossless JPEG rotation / cropping (jpegtran)
- **Shipped with the program**: ffmpeg, Ghostscript, and jpegtran are all bundled inside the program directory — no codecs or system extensions are required
- **System integration**: Explorer right-click "Browse with Gaze", Open With registration, single-instance running (launching again brings up the existing window)
- **Search images by text**: integrates a local semantic retrieval service (CLIP + OCR + file names) for natural-language image search

---

## 📦 Installation

**Portable (recommended)**: place the program directory anywhere and double-click `Gaze.exe` to run.

- All settings are stored in `Gaze.ini` inside the program directory, with the thumbnail cache in `thumbnails.db`
- No registry writes (only optional one-time system integration on first run); migration is simply copying the directory

## 🔨 Building from source

```
Dependencies: CMake ≥ 3.16, Qt 6.8.3 (win64_mingw), MinGW 13.1.0 (SEH)
External libraries: ffmpeg / Ghostscript / jpegtran are bundled with the repo under vendor/; LibRaw sits under thirdparty/
```

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The build script automatically syncs `assets/` and `vendor/` into the build directory, so the output runs as-is.

## ⌨️ Keyboard shortcuts (selected)

| Key | Action |
|---|---|
| Enter / double-click | Enter the viewer (browser ↔ viewer toggle) |
| ESC | Return to the browser |
| G | Fullscreen preview (image only; layout restored exactly on exit) |
| F11 | Fullscreen UI |
| Space | Default action / play |
| Ctrl+F | Inline search |
| F3 | Rename |
| Ctrl+W | Close the current tab |
| Ctrl+1~5 | Color labels |
| Long-press left button | 1:1 pixel view (cursor-focused) |

## 🗂 Project structure

```
src/app/       Application shell: main window, settings, themes
src/views/     File grid (virtualized custom-drawn), directory tree, preview panel, context menus
src/media/     Thumbnail engine, motion photo parsing, color label store
src/dialogs/   Settings pages (20 category pages), printing, text-based image search, database maintenance
vendor/        External tools shipped with the program (ffmpeg, Ghostscript, jpegtran)
thirdparty/    Third-party source compiled in (LibRaw, etc.)
```

## 🧭 Roadmap

- [ ] Polish the light theme and runtime theme hot-switching
- [ ] Fully built-in HEIF / CMYK decoding (removing the dependency on system WIC extensions)
- [ ] Field-tested coverage of more RAW vendor formats
- [ ] Deep integration of text-based image search (directory management and model management UIs)

## 🙏 Acknowledgements

- [Qt](https://www.qt.io/) — application framework
- [LibRaw](https://www.libraw.org/) — RAW decoding
- [FFmpeg](https://ffmpeg.org/) — audio/video decoding
- [Ghostscript](https://ghostscript.com/) — PDF rendering
- [jpegtran](https://jpegclub.org/) — lossless JPEG operations
- [XnView MP](https://www.xnview.com/en/xnviewmp/) — design reference for the interface and interactions

## 📄 License

Private project, not yet licensed for public distribution. Please contact the author if you wish to use it.

---

### 📎 About this repository (internal notes)

Build outputs are committed to this repository for direct verification. The single ledger for development progress and task specs is [`todo.md`](todo.md).
