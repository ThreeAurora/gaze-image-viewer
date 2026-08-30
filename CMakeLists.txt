cmake_minimum_required(VERSION 3.16)
project(gaze VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Widgets
    Network
    Multimedia
    MultimediaWidgets
    Sql
    PrintSupport
)

# FFmpeg
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG IMPORTED_TARGET
        libavformat libavcodec libavutil libswscale)
endif()

if(NOT FFMPEG_FOUND)
    if(DEFINED FFMPEG_ROOT)
        set(FFMPEG_INCLUDE_DIRS "${FFMPEG_ROOT}/include")
        set(FFMPEG_LIBRARY_DIRS "${FFMPEG_ROOT}/lib")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/ffmpeg/include")
        set(FFMPEG_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/ffmpeg/include")
        set(FFMPEG_LIBRARY_DIRS "${CMAKE_SOURCE_DIR}/ffmpeg/lib")
    else()
        message(WARNING "FFmpeg not found. Video thumbnails will use QProcess fallback.")
    endif()
endif()

# ── 源文件按子系统分组(2026-09-01 #129 解耦重构)
# 目录:src/app 装配与主窗口 | src/views 界面控件 | src/media 图像/缩略图管线 |
#       src/dialogs 对话框。头文件一律留在 src/(它在 include 路径上,源文件里的
#       #include "xxx.h" 不用改)。源文件不再与同名 .h 同目录 → AUTOMOC 无法靠
#       "同目录同名"发现它们,故把 src 下所有 .h 显式加进目标(见 GAZE_HEADERS)。────────────────────────
# 巨型类不再塞在单个 .cpp:同一个类的成员函数按职责拆成多个编译单元(TU)。
# 跨 TU 共享的文件级 helper 收进各自的 *_internal.h(inline + 具名 namespace;
# 带状态的改成"返回函数局部 static 引用的访问器",保住全程序单实例语义)。
# 规矩:给某个类加实现就放进它对应的那一组,不要再往主 .cpp 里续。
file(GLOB_RECURSE GAZE_HEADERS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.h")

add_executable(gaze WIN32
    ${GAZE_HEADERS}
    src/app/main.cpp

    # 主窗口:装配 / 菜单工具栏 / 导航 / 查看器形态 / 标签 / 键鼠路由 / 动作槽
    src/app/mainwindow.cpp
    src/app/mainwindow_menus.cpp
    src/app/mainwindow_nav.cpp
    src/app/mainwindow_viewer.cpp
    src/app/mainwindow_tabs.cpp
    src/app/mainwindow_keys.cpp
    src/app/mainwindow_ops.cpp
    src/views/foldertree.cpp

    # 文件列表:模型与载入 / 几何布局 / 绘制 / 输入与选择 / 排序筛选 / 内联搜索
    src/views/filegrid.cpp
    src/views/filegrid_layout.cpp
    src/views/filegrid_paint.cpp
    src/views/filegrid_input.cpp
    src/views/filegrid_sort.cpp
    src/views/filegrid_find.cpp
    # src/filecard.cpp:FileGrid 已改单画布自绘,逐条目子控件不再参与构建
    src/views/sortheader.cpp

    # 预览/查看器:生命周期 / 图片与缩放 / 视频音频与 Live Photo / GIF /
    #             文本-PDF-Markdown / 叠加层与外观 / 事件入口
    src/views/previewpanel.cpp
    src/views/previewpanel_image.cpp
    src/views/previewpanel_media.cpp
    src/views/previewpanel_gif.cpp
    src/views/previewpanel_text.cpp
    src/views/previewpanel_chrome.cpp
    src/views/previewpanel_events.cpp

    # 缩略图:调度与线程池 / 缓存与数据库 / 静图 / 视频 / 文件夹外框
    src/media/thumbnailer.cpp
    src/media/thumbnailer_cache.cpp
    src/media/thumbnailer_image.cpp
    src/media/thumbnailer_video.cpp
    src/media/thumbnailer_folder.cpp
    src/media/livephoto.cpp
    src/media/labelstore.cpp

    # 设置:对话框骨架 + 五组分页
    src/app/settings.cpp
    src/app/theme.cpp
    src/dialogs/settings_dialog.cpp
    src/dialogs/settings_pages_basic.cpp
    src/dialogs/settings_pages_input.cpp
    src/dialogs/settings_pages_visual.cpp
    src/dialogs/settings_pages_view.cpp
    src/dialogs/settings_pages_advanced.cpp

    # 其余对话框与打印
    src/dialogs/dbmaintenance.cpp
    src/views/contextmenu.cpp
    src/dialogs/imgsearchdialog.cpp
    src/dialogs/searchdialog.cpp
    src/dialogs/printlayout.cpp
    src/dialogs/printdialog.cpp

    src/resource.rc
    src/resources.qrc
)

target_link_libraries(target_link_libraries(gaze PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Sql
    Qt6::PrintSupport
    Qt6::PrintSupport
    Qt6::PrintSupport
    Qt6::PrintSupport
    shell32
    ole32
    comctl32    # SHGetImageList / IImageList::GetIcon
    uuid        # IID_IImageList 等系统 GUID 符号
)

target_include_directories(Gaze PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(FFMPEG_FOUND)
    if(TARGET PkgConfig::FFMPEG)
        target_link_libraries(Gaze PRIVATE PkgConfig::FFMPEG)
    else()
        target_include_directories(Gaze PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_directories(Gaze PRIVATE ${FFMPEG_LIBRARY_DIRS})
        target_link_libraries(Gaze PRIVATE avformat avcodec avutil swscale)
    endif()
    target_compile_definitions(Gaze PRIVATE HAS_FFMPEG=1)
endif()

target_compile_options(Gaze PRIVATE -O2 -finput-charset=UTF-8 -fexec-charset=UTF-8)
target_link_options(Gaze PRIVATE -static-libgcc -static-libstdc++)

# ── assets 自动部署 ──
# IconLib::appIcon() 是运行时从 exe 旁 assets/ 读 PNG 的,windeployqt 不管应用自有资源。
# 换构建目录(或新克隆)忘了拷就会出现"所有图标变空白按钮"(2026-08-31 事故),
# 这里用 post-build 无条件同步,杜绝复发。
add_custom_command(TARGET gaze POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/src/assets" "$<TARGET_FILE_DIR:gaze>/assets"
    COMMENT "同步应用图标资源 assets/ 到构建目录"
    VERBATIM
)

# ── Ghostscript 自动部署(PDF 预览)──
# 用户 2026-08-31 明令:PDF 预览用随 gaze 分发的 Ghostscript,不去搜系统目录。
# 必须整目录拷:gs\bin\gswin64c.exe 靠相对自身exe的 ../Resource 找 gs_init.ps,
# 还要 gsdll64.dll 与 lib/ 里的 PDF 相关初始化文件 —— 只拷 exe 会直接报错。
add_custom_command(TARGET gaze POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/vendor/gs" "$<TARGET_FILE_DIR:gaze>/gs"
    COMMENT "同步内置 Ghostscript vendor/gs -> 构建目录 gs/"
    VERBATIM
)

# ── 内置 Ghostscript 部署(PDF 预览)──
# 用户 2026-08-31 明令:PDF 预览用随 gaze 分发的 Ghostscript,而不是搜系统装没装。
# 必须整目录拷:gs\bin\gswin64c.exe 靠"相对自身 exe 的 ../Resource"找 gs_init.ps,
# 还要同目录 gsdll64.dll 与 lib/ 的初始化文件 —— 只拷 exe 会直接报错退出。
add_custom_command(TARGET gaze POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/vendor/gs" "$<TARGET_FILE_DIR:gaze>/gs"
    COMMENT "同步内置 Ghostscript vendor/gs -> 构建目录 gs/"
    VERBATIM
)

# ── 内置 ffmpeg 部署(视频缩略图,#112)──
# 与 Ghostscript 同一套思路:exe 旁 ffmpeg/ 永远优先于 PATH,机器上装没装 ffmpeg
# 不再决定缩略图长什么样。共享构建的 ffmpeg.exe 靠同目录的 av*.dll,必须整目录拷。
# 但"把 ffmpeg.exe 随 gaze 分发出去"是许可决策(BtbN 同一版本有 LGPL 与 GPL 两种构建,
# LGPL 那份已经带 dav1d,解码缩略图够用),2026-08-31 待用户裁决 ——
# 所以这里只在 vendor/ffmpeg/ 真的存在时才拷:没裁决 = 目录不存在 = 不拷。
if(EXISTS "${CMAKE_SOURCE_DIR}/vendor/ffmpeg")
    add_custom_command(TARGET gaze POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/vendor/ffmpeg" "$<TARGET_FILE_DIR:gaze>/ffmpeg"
        COMMENT "同步内置 ffmpeg vendor/ffmpeg -> 构建目录 ffmpeg/"
        VERBATIM
    )
else()
    message(STATUS "视频缩略图:vendor/ffmpeg 不存在(分发决策未定)→ 运行时回退 PATH 上的 ffmpeg")
endif()

# ── Ghostscript 自动部署(PDF 预览)──
# 用户 2026-08-31 明令:PDF 预览用随 gaze 分发的 Ghostscript,不去搜系统目录。
# 必须整目录拷:gs\bin\gswin64c.exe 靠相对自身exe的 ../Resource 找 gs_init.ps,
# 还要 gsdll64.dll 与 lib/ 里的 PDF 相关初始化文件 —— 只拷 exe 会直接报错。
add_custom_command(TARGET gaze POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/vendor/gs" "$<TARGET_FILE_DIR:gaze>/gs"
    COMMENT "同步内置 Ghostscript vendor/gs -> 构建目录 gs/"
    VERBATIM
)

# ── 内置 ffmpeg 部署(视频缩略图,#112)──
# 与 Ghostscript 同一套思路:exe 旁 ffmpeg/ 永远优先于 PATH,机器上装没装 ffmpeg
# 不再决定缩略图长什么样。共享构建的 ffmpeg.exe 靠同目录的 av*.dll,必须整目录拷。
# 但"把 ffmpeg.exe 随 gaze 分发出去"是许可决策(BtbN 同一版本有 LGPL 与 GPL 两种构建,
# LGPL 那份已经带 dav1d,解码缩略图够用),2026-08-31 待用户裁决 ——
# 所以这里只在 vendor/ffmpeg/ 真的存在时才拷:没裁决 = 目录不存在 = 不拷。
if(EXISTS "${CMAKE_SOURCE_DIR}/vendor/ffmpeg")
    add_custom_command(TARGET Gaze POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/vendor/ffmpeg" "$<TARGET_FILE_DIR:Gaze>/ffmpeg"
        COMMENT "同步内置 ffmpeg vendor/ffmpeg -> 构建目录 ffmpeg/"
        VERBATIM
    )
else()
    message(STATUS "视频缩略图:vendor/ffmpeg 不存在(分发决策未定)→ 运行时回退 PATH 上的 ffmpeg")
endif()

# ── 内置 jpegtran 部署(无损旋转/镜像,#113)──
# 与 ffmpeg 同一套 vendor 规约:exe 旁 jpegtran/ 优先于 PATH。jpegtran.exe
# 靠同目录 jpeg8.dll(libjpeg-turbo, IJG+BSD 许可文本同目录),必须整目录拷。
if(EXISTS "${CMAKE_SOURCE_DIR}/vendor/jpegtran")
    add_custom_command(TARGET Gaze POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/vendor/jpegtran" "$<TARGET_FILE_DIR:Gaze>/jpegtran"
        COMMENT "同步内置 jpegtran vendor/jpegtran -> 构建目录 jpegtran/"
        VERBATIM
    )
else()
    message(STATUS "无损旋转:vendor/jpegtran 不存在 → 运行时回退 PATH/常见安装位置上的 jpegtran")
endif()

# ── LibRaw 源码编译(RAW 按需全解,#140)──
# 全囊括铁令(2026-09-01):一切功能自带依赖,不默认用户装有 WIC Raw 图像扩展
# 等系统解码器。LibRaw(LGPL-2.1/CDDL 双许可)源码放 thirdparty/LibRaw,静态
# 编进 gaze —— 无 DLL、无分发决策。目录不存在(未下载)时跳过:gaze 照常编译,
# RAW 按钮点击给"未编入解码器"的诚实文案。
# 无外部依赖构建(不开 USE_JPEG/USE_ZLIB/USE_LCMS2):无损 JPEG 系 RAW
# (CR2/NEF/ARW/RAF/ORF/DNG 无损变体等)全覆盖;JPEG 有损压缩与 deflate DNG
# 后续按需补库。
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/LibRaw/libraw/libraw.h")
    file(GLOB_RECURSE LIBRAW_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/thirdparty/LibRaw/src/*.cpp")
    list(APPEND LIBRAW_SOURCES
        "${CMAKE_SOURCE_DIR}/thirdparty/LibRaw/internal/dcraw_common.cpp"
        "${CMAKE_SOURCE_DIR}/thirdparty/LibRaw/internal/dcraw_fileio.cpp"
        "${CMAKE_SOURCE_DIR}/thirdparty/LibRaw/internal/demosaic_packs.cpp")
    add_library(rawdec STATIC ${LIBRAW_SOURCES})
    target_include_directories(rawdec PUBLIC "${CMAKE_SOURCE_DIR}/thirdparty/LibRaw")
    # dcraw 血统代码有类型双关,-fno-strict-aliasing 保优化下行为不漂
    target_compile_options(rawdec PRIVATE -O2 -fno-strict-aliasing)
    target_link_libraries(Gaze PRIVATE rawdec)
    target_compile_definitions(Gaze PRIVATE HAS_RAWDEC=1)
else()
    message(STATUS "RAW 全解:thirdparty/LibRaw 不存在 → RAW 按钮显示未编入提示")
endif()
