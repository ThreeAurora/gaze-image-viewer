# Gaze — Complete Feature List (FEATURES)

[中文](./FEATURES.md) | English

An **exhaustive list** of what Gaze can actually do, compiled from: the full development history, the `todo.md` ledger, all 166 settings keys in SETTINGS_MATRIX.md, and a source-level scan of menus / shortcuts / extension whitelists. The README keeps only a curated facade — this is everything. Per-key settings status lives in [SETTINGS_MATRIX.md](SETTINGS_MATRIX.md).

## 1. Views & Browsing

- Three-pane layout: directory tree / file grid / preview panel; six pane types (tree, preview, address bar, toolbar, status bar, info bar) individually toggleable and persisted
- 8 view modes: thumbnails, thumbnails + names, + labels, + details, icons, list, details, waterfall
- Layout schemes: follow last window state; save current layout (named, reserved-name guard); delete layouts; each stores geometry / splitter / panes
- Multi-tab: viewer-mode top tab bar; persistent "Browser" tab; 32px thumbnail per tab; Ctrl+W closes; closing the last image tab returns to the browser; multi-tab / single-tab / cap (default 20) per file configurable
- Back / forward navigation (Alt+←/→, greyed at the ends); going up auto-selects the child folder just left
- F5 refresh (same-folder reload keeps scroll position); scroll resets to top when changing folders
- Address bar: click selects all, second click places the caret, Enter jumps to a folder or file, quoted paths accepted, jumps locate and select the target with edge-snapping scroll, a 500 ms Enter grace period prevents accidental viewer opens, Alt+Backspace deletes a character, Ctrl+Backspace a word, path history dropdown
- Entering a folder pre-selects the first item and syncs the preview; selected rows auto-scroll into view
- Fast-scroll rendering optimizations; files must never be half-visible
- Two-way auto-sync between the tree and the current folder; tree scrolls the current item into view
- Left-button press switches folders instantly (expand-arrow column exempt); holding and sweeping switches each folder in turn; "switch vs. drag-select" toggle
- Show "Desktop" in tree; show hidden items
- Drag & drop: navigate / move from Explorer, Ctrl+drop copies, optional confirmation; forbidden cursor when dropping outside the grid/tree
- Window title templates: separate browser/viewer templates with {folder}/{filename} placeholders
- Startup behavior: with file / without file / specific folder, remember filename, restore last folder/file, open list + preview at start
- Single instance: relaunching raises the existing window via IPC and passes the path; CLI file arguments open directly

## 2. Thumbnails

- Three-tier decode pipeline: native Qt decode → Windows Shell thumbnails → external ffmpeg fallback; graceful degradation with a one-shot log hint
- Four-in-one folder thumbnails: XnView MP style (folder silhouette with a 2×2 montage), fewer cells shown if fewer files; falls back to natural-order scanning of direct subfolders; video cells via in-process ffmpeg single-frame; each cell fetched at max(256, 2×cell) then downsampled
- Single-cover folder mode (first candidate in folder when 4-in-1 is off)
- Video thumbnails: configurable frame position (default 80%), sub-second videos retry past EOF, ffprobe duration probe cached
- HDR video thumbnails: PQ/HLG detection → zscale + tonemap second pass (zero cost for SDR)
- Sizes: 8 presets + custom (48–1024 px), Ctrl+=/Ctrl+- or wheel zoom
- Thumbnail cache thumbnails.db (SQLite WAL, per-thread connections); cache generations force regeneration after settings changes
- Hidden files tinted; fallback folder icons generated at runtime and cached per size
- Thumbnail database maintenance dialog: stats / per-folder aggregation / clear / rebuild

## 3. Preview & Viewer

- Eight preview types: images / GIF / video / audio / txt / Markdown / PDF / RAW
- Viewer: Enter/double-click in, ESC back; double-click routing (images & videos open the viewer, other types go to the system default app, 300 ms threshold)
- Zoom: Ctrl+wheel cursor-centered zoom, long-press for true 1:1 pixels (cursor-focused), drag panning, navigator mini-map with draggable blue frame
- Auto-fit profiles for normal and fullscreen, reset-on-navigate toggle, separate in/out scaling filters, 10 pixel-ratio entries, HiDPI 1px=1px, gamma correction, sharpening, two-pass rendering, read-ahead one image / keep current
- Viewer appearance: background color, checkerboard padding, borders, scrollbars, pan tool, selection overlay, color-label display
- Three fullscreen layers: F11 fullscreen UI / G fullscreen preview (image only, exact layout restore on exit) / in-viewer fullscreen; floating toolbar, hide cursor, info badge (size/zoom%), playbar/toolbar/scrollbar visibility, dual-monitor choice — an independent fullscreen config group; preview context menu gains "Fullscreen Preview  (G)" for images/videos/audio alike (#224); G fullscreen film strip = every file in the folder participates (not just images; thumbnail-less cells draw the file name; RAW counts as image but never enters the thumbnail queue), three check buttons on the right — images/videos/audio — toggle categories on/off (all on by default, persisted in ini, #225/#226)
- Text preview: txt toggle + auto-truncation for long files
- Markdown rendered preview (custom renderer: headings, bold/italic/strike, inline & fenced code, quotes, lists, task lists, links, images, rules, tables, autolinks)
- PDF preview: bundled Ghostscript (vendor/gs, AGPL license included)
- RAW preview: placeholder + "Load original RAW" button, background-thread LibRaw full decode, stale-generation abort when switching files, full zoom/pan/viewer takeover on success
- Mouse wheel over preview text switches files (never scrolls the text)
- Info panel (F9): file properties + image dimensions + grouped EXIF tree; RGB histogram (three channels blended + luma line, background sampling)
- Video switch flash eliminated: native video surface hidden until the first frame arrives

## 4. Video Playback

- Playback bar: play/pause, stop, volume button + numeric level, click/drag seeking, adaptive time display, remaining-time toggle
- Left-click on video toggles play/pause; autoplay and loop toggles
- Seek Shift+PgUp/PgDn, step 1–3600 s configurable (moved from Ctrl+PgUp/PgDn in #221; those keys now switch tabs)
- GIF engine: QImageReader frame-by-frame decoding; bottom-anchored progress bar, click to seek, bidirectional scrubbing, end-of-clip rewind, 48 MB frame cache evicted around the playhead; animation-disable toggle (GIF/JIF/APNG/ANI)
- Motion photos: Apple Live Photo paired videos + Google/Samsung embedded-MP4 dual protocol with four-stage detection; faststart repair for embedded videos; click to play, auto-return to still; right-click "Play live video" / "Extract frames"; audio-companion autoplay toggle
- AV1: libdav1d-enabled avcodec + rejecting faulty hardware-decode devices (both required), full-speed software decode
- HDR video: VP9 10-bit / BT.2020 / PQ (HDR10) 60 fps continuous playback

## 5. Audio & Waveform

- 10 audio extensions (mp3/wav/flac/aac/ogg/wma/m4a/opus/amr/ac3) with preview-panel playback + controls
- Waveform: dedicated low-priority thread, streamed decode, 512-bucket min/max, progressive drawing, generation-gated stale frames; failures only show "waveform unavailable" and never block browsing (performance red line)
- Multi-format samples (Int16/Int32/Float/UInt8), cross-channel extremes
- 8/8 audio codecs field-tested (ac3/amr/flac/m4a/mp3/ogg/opus/wav)

## 6. Format & Decode Coverage (charter: all formats)

| Category | List |
|---|---|
| Images (33) | jpg jpeg jfif png gif bmp webp heic heif hif avif avifs jxl tiff tif ico svg svgz tga icns wbmp pbm pgm ppm xbm xpm cur exr dds qoi jp2 dpx apng |
| RAW (26) | cr2 cr3 crw nef nrw arw srf sr2 dng orf rw2 raf pef erf rwl 3fr fff gpr kdc k25 mef mrw x3f mos srw iiq |
| Video (28) | mp4 mov avi mkv webm wmv flv m4v mpg mpeg 3gp ts m2ts mts vob ogv divx rm rmvb asf f4v avchd mxf qt 3g2 ogm m1v m2v |
| Audio (10) | mp3 wav flac aac ogg wma m4a opus amr ac3 |
| Documents (14) | txt doc docx pdf rtf odt xls xlsx ppt pptx csv md epub mobi |
| Executables (12) | exe bat cmd ps1 sh msi com scr vbs jar py pl rb |

- Routed decode pipeline: AVIF/JXL → ffmpeg (libdav1d/libjxl); HEIC/HEIF/HIF → WIC; CMYK JPEG → WIC color-managed decode; everything else native Qt
- File-header sniffing (AVIF ftyp brands, JXL magic), identify by extension or by scanning headers
- RAW whitelist kept out of thumbnail/read-ahead pipelines entirely; LibRaw 0.21.4 statically linked (real CR2 sample: 3881 ms to 6264×4180)
- Every decode dependency ships with the program: vendor/ffmpeg, vendor/gs, vendor/jpegtran, thirdparty/LibRaw — no preinstalled components assumed

## 7. File Management & Deletion

- File context menu (~25 items): open / fullscreen / open with / show in Explorer / open all selected / open in new tab / cut / copy / paste / copy to… / move to… / delete / rename / duplicate / new folder / print (Ctrl+P) / rotate & flip (90° L/R, horizontal, vertical) / lossless crop / color-label submenu / properties / play live video / extract frames
- Blank-area context menu: new folder / show in Explorer / select all / properties
- Right-clicking an unselected item selects it first (Explorer behavior)
- Folder-tree context menu (13 items): new / cut / copy / paste / delete / rename / copy to / move to / show files in subfolders / search / open in Explorer / properties
- Deletion: unified Recycle-Bin path (SHFileOperationW ALLOWUNDO + WANTNUKEWARNING); Del/S keys; confirmation (separate folder switch); next item auto-selected after delete; bottom-left toast (text/color/duration configurable)
- Clipboard: cut / copy / paste (Ctrl+C/X/V); paste name collisions follow the "duplicate naming" template
- Duplicate, copy-to/move-to dialogs, new folder (X key)
- Rename: F2/F3 focus-routed (tree & grid); dialog or in-card inline rename; name validation (rejects path separators / illegal characters)
- Recent files menu: 0–100 cap, clear-on-exit toggle

## 8. Sort · Filter · Search

- 16 sortable columns: name / extension / modified / created / EXIF capture date / EXIF modified / type / size / image size / width / height / orientation / ratio / print size / path / color label — ascending or descending
- Lightweight EXIF parsing (first 64 KB only); creation date via ctime
- Three name orders: numeric (natural sort) / alphabetical / normal
- 10 startup-sort presets (including "remember last")
- Mixed file/folder sorting, folders always alphabetical, new files at end, auto-select new files, byte-based sizes, show subfolder files
- 18 filter modes: all / images (+folders) / videos (+folders) / audio / archives / documents / executables / folders / custom extensions / red-orange-yellow-green-blue labels / non-red
- Custom extension-set filter (persisted)
- Toolbar filter dropdown with indicator sync
- Inline search Ctrl+F: search-as-you-type, previous/next, highlight, greyed when empty
- Name search dialog: include/exclude regexes, recursive, include hidden, include folders; time-budgeted scanning never freezes the UI, results stream in, depth/hit caps reported honestly
- Search images by text (wanxiang-imgseek client, Ctrl+Shift+F): backend service is NOT auto-started by default (enable auto-start in Settings → Search images by text), model switching (Chinese/English CLIP), sorting, result grid with per-source score badges, right-click locate/open/copy path, double-click reveals in the main window, service lifecycle management

## 9. Settings (~166 keys + 3 composite groups, 20 category pages)

Per-key status in [SETTINGS_MATRIX.md](SETTINGS_MATRIX.md). Group overview:

- **General/Start**: single instance, EXIF-rotation, DPI adjustments, session-save modes; with/without-file startup
- **Browser**: start dir / last dir & file, preview back color, label display, view mode / name order / filter mode / custom extensions / startup sort / path history — 20 keys
- **FileList**: hidden items, identification (extension / header-scan, 3 levels), mixed sort, new-file placement, byte sizes, subfolders — 10 keys
- **FileOps**: delete confirmation (files/folders), recycle bin, delete toast & duration, drop confirmation, lossless backup / keep metadata, rename style, duplicate-naming template
- **Appearance**: thumbnail size, shadow / border / spacing, label alignment, extension→color mapping (editable color editor), dark/light theme
- **Viewer/Preview**: auto-fit, zoom mode, filters, pixel ratio, background/padding/border/scrollbar, autoplay/loop, disable animation, gamma, sharpening, seek seconds, list looping, two-pass render, read-ahead; txt/MD/PDF toggles
- **Keyboard/Mouse**: arrow keys / Space / ESC behavior; 12 modifier-composed mouse & wheel bindings
- **Interface**: multi/single tab & cap, sync viewer to browser, startup panes, recent files cap & cleanup, title templates ×2, slideshow interval
- **LabelColors**: extension→name-background mapping + fallback color
- **Print**: 15 fully persisted print-dialog keys (printer/paper/copies/landscape/per-page/fit/margins/gaps/caption/font size/grayscale/background/border/subset/range)
- **ImgSearch**: port / directory / python / auto-start / kill-on-exit
- **Integration**: Explorer context-menu integration (incl. directory background), portable vs %APPDATA% ini location migration
- **Layout/Fullscreen**: layout-scheme group; independent fullscreen group
- **Shortcuts**: every browser menu action with a shortcut is rebindable + 8 viewer actions

## 10. UI & Themes

- Dark/light dual themes: 30+ color tokens + app-wide QSS, switched via View → Theme (restart to apply)
- Layered greys: menubar > toolbar > tree > list > preview; Win-style arrowed scrollbars with 4 states
- XnView MP-style polish throughout: sort header, context menus, playback bar, unified black background
- Five toolbar menu buttons with ▼ indicators; column-count dropdown; all menu/toolbar check states persisted
- Settings dialog: 5 pages / 20 categories, search filter, compaction, zero dead entries
- Hover highlights, card selection, pixel-aligned blue selection frame

## 11. Shortcuts (full table)

C/←/↑ previous · V/→/↓ next · Space play/pause · Ctrl+PgUp/PgDn switch left/right tab · Shift+PgUp/PgDn seek · hold right button + wheel zoom · double-click preview area open tab (browser) / close tab (viewer) · F5 refresh · F11 fullscreen UI · G fullscreen preview · ESC back · Alt+←/→ back/forward · Backspace up · Alt+Backspace address-bar delete · Ctrl+1~5 color labels · Ctrl+0/D clear · F red label · F2/F3 rename · Del/S delete · X new folder · Enter/double-click viewer · Ctrl+A select all · Ctrl+I invert · Ctrl+F inline search · Ctrl+W close tab · Ctrl+Shift+T reopen closed tab · Ctrl+P print · Ctrl+O open · Ctrl+C/X/V clipboard · Ctrl+Shift+F image search · F12 settings · F9 info panel · Space confirms dialogs · bare keys never hijack text edits

## 12. Print · Edit · Metadata

- Print: printer selection (guidance when none), paper, copies, landscape, per-page layout, fit mode, margins/gaps in mm, caption + font size, grayscale, background, border, page subset/range; all 15 keys persisted; multi-image layout engine; right-click & File menu entries
- Lossless rotate/flip: jpegtran -copy all with timestamp restore; EXIF-only rotation toggle; lossless / backup / keep-metadata toggles; non-JPEG via ffmpeg lossless transforms
- Lossless crop: drag a region on the zoomed preview → snap to 16-pixel MCU boundaries → jpegtran -perfect verification
- EXIF metadata: two-tier readers, grouped display in the info panel; EXIF auto-rotation
- Motion-photo frame extraction (ffmpeg)

## 13. Stability & Diagnostics

- Gaze.log full-level categorized logging; startup checkpoint chain (millisecond timings); GUI heartbeat + watchdog (>12 s alert); crash minidumps
- perf.log performance log (hot-path timings)
- Single-instance IPC, double-click standalone-open fast path, QSettings thread safety, unified CREATE_NO_WINDOW for external tools
- Deletion always through the Recycle Bin (iron rule); parameterized escaping for DB maintenance

## 14. Misc

- Portable: Gaze.ini + thumbnails.db inside the program directory, no registry writes; migration = copying the folder
- System integration: Explorer directory/background context menu "Browse with Gaze", Open With registration
- Build: CMake + Qt 6.8.3 MinGW, POST_BUILD syncs assets/vendor

## Appendix: Claims & Boundaries (honest notes)

- **PCX/PSD** appear in neither the image whitelist nor the decode probes — earlier README tables over-claimed them and have been corrected; this document is the authoritative claim set
- EXR/DDS/JP2 decoders are in place but sample verification is pending
- HEIF/CMYK currently decode through the system WIC channel — a known exception to the "all dependencies bundled" rule (see Roadmap)
- The light theme is implemented (restart to switch); runtime hot-switching remains on the Roadmap
- Explicitly rejected (implementing these would be a mistake): batch rename, slideshow, batch convert, type badges, star marks/ratings, changing the base framework, two-level progressive loading, depending on system components by default
