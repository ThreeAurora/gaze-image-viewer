#include "settings_dialog.h"
#include "settings.h"
#include "integration.h"
#include "labelstore.h"
#include "constants.h"
#include "dbprefix.h"
#include "viewerhotkeys.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QMessageBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QMenuBar>
#include <QScrollArea>
#include <QFrame>
#include <QTreeWidget>
#include <QListWidget>
#include <QPalette>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QMenu>
#include <QColorDialog>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSet>
#include <functional>
#include "dbmaintenance.h"
#include "settings_dialog_internal.h"

using namespace sd_impl;   // colorPick

QWidget* SettingsDialog::pageViewer() {
    auto* root = new QVBoxLayout;
    root->setSpacing(6);   // 分组框页:组间距收紧(组框自身已有边距)

    // 分组"缩放"
    auto* fZoom = new QFormLayout;
    fZoom->setVerticalSpacing(6);
    fZoom->addRow(QString::fromUtf8("自动缩放"),
        combo("Viewer/autoFit", {QString::fromUtf8("上次使用过的"), QString::fromUtf8("不缩放"),
            QString::fromUtf8("适应窗口"), QString::fromUtf8("适应窗口大小 (仅小图片)"),
            QString::fromUtf8("适应窗口大小 (仅大图片)"), QString::fromUtf8("适应窗口宽度"),
            QString::fromUtf8("适应窗口高度"), QString::fromUtf8("适应窗口宽或高"),
            QString::fromUtf8("适应桌面"), QString::fromUtf8("窗口适应到图像")}, 2));
    fZoom->addRow(chk("Viewer/resetAutoOnNav", QString::fromUtf8("使用下一个/上一个文件重置'自动图像尺寸'设置"), false));
    fZoom->addRow(QString::fromUtf8("缩放率"),
        combo("Viewer/zoomMode", {QString::fromUtf8("固定"), QString::fromUtf8("变动")}, 1));
    fZoom->addRow(QString::fromUtf8("缩小抗锯齿"),
        combo("Viewer/outZoomFilter", {"无", "Bilinear", "Bicubic", "Spline 16",
            "Spline 36", "Lanczos 3", "Lanczos 4"}, 1));
    fZoom->addRow(QString::fromUtf8("放大抗锯齿"),
        combo("Viewer/inZoomFilter", {"无", "Bilinear", "Bicubic", "Spline 16",
            "Spline 36", "Lanczos 3", "Lanczos 4"}, 1));
    fZoom->addRow(chk("Viewer/hidpiPixel", QString::fromUtf8("在 HiDPI 屏幕上缩放:1 图像像素 = 1 屏幕像素"), false));
    fZoom->addRow(QString::fromUtf8("像素比"),
        combo("Viewer/pixelRatio", {"1.00 正方形", "0.91 D1/DV NTSC", "0.95 D4/D16 Standard",
            "1.09 D1/DV PAL", "1.20 D1/DV NTSC Widescreen", "1.33 HDV 1080/DVCPRO HD 720",
            "1.46 D1/DV PAL Widescreen", "1.50 DVCPRO HD 1080", "1.90 D4/D16 非变形",
            "2.00 变形"}, 0));
    root->addWidget(group(QString::fromUtf8("缩放"), fZoom));

    // 分组"背景与界面元素"
    auto* fUI = new QFormLayout;
    fUI->setVerticalSpacing(6);
    fUI->addRow(QString::fromUtf8("背景色"),
                colorPick("Viewer/backColor", "#000000"));
    fUI->addRow(chk("Viewer/checkerMode", QString::fromUtf8("背景以挡板模式显示"), false));
    fUI->addRow(chk("Viewer/showBorder", QString::fromUtf8("显示边框"), false));
    fUI->addRow(chk("Viewer/panTool", QString::fromUtf8("显示平移工具"), true));
    fUI->addRow(chk("Viewer/showRating", QString::fromUtf8("显示颜色标记"), true));
    fUI->addRow(chk("Viewer/showScrollbar", QString::fromUtf8("显示滚动条"), false));
    fUI->addRow(QString::fromUtf8("选中的"),
        combo("Viewer/selectedOverlay", {QString::fromUtf8("正常"),
            QString::fromUtf8("三分法"), QString::fromUtf8("黄金分割(Phi)")}, 0));
    root->addWidget(group(QString::fromUtf8("背景与界面元素"), fUI));
    return wrapTitled(QString::fromUtf8("查看"), root);
}

// ── 查看 → 其他:播放与性能(从查看页拆出,页面不再过长) ──
QWidget* SettingsDialog::pageViewerOther() {
    auto* fPlay = new QFormLayout;
    fPlay->setVerticalSpacing(6);
    fPlay->addRow(chk("Viewer/autoPlayVideo", QString::fromUtf8("自动播放(视频)"), true));
    fPlay->addRow(chk("Viewer/loopVideo", QString::fromUtf8("循环视频播放"), false));
    fPlay->addRow(chk("Viewer/autoPlayAudioCompanion", QString::fromUtf8("自动播放音频伴侣文件"), false));
    fPlay->addRow(chk("Viewer/loopFileList", QString::fromUtf8("循环文件列表"), false));
    fPlay->addRow(chk("Viewer/twoPassRender", QString::fromUtf8("加载时两段式渲染"), false));
    fPlay->addRow(chk("Viewer/readAhead", QString::fromUtf8("预先读取一幅图像"), true));
    fPlay->addRow(chk("Viewer/cacheBehind", QString::fromUtf8("保持当前图像"), true));
    fPlay->addRow(chk("Viewer/disableAnimation", QString::fromUtf8("禁用 GIF/JIF/APNG/ANI 动画"), false));
    fPlay->addRow(chk("Viewer/gamma", QString::fromUtf8("使用 Gamma 纠正"), false));
    fPlay->addRow(chk("Viewer/sharpen", QString::fromUtf8("使用锐化 50%"), false));

    auto* root = new QVBoxLayout;
    root->setSpacing(6);   // 分组框页:组间距收紧(组框自身已有边距)
    root->addWidget(group(QString::fromUtf8("播放与性能"), fPlay));
    return wrapTitled(QString::fromUtf8("其他"), root);
}

QWidget* SettingsDialog::pageFullscreen() {
    auto* root = new QVBoxLayout;
    root->setSpacing(6);   // 分组框页:组间距收紧(组框自身已有边距)

    // 分组"显示"
    auto* fShow = new QFormLayout;
    fShow->setVerticalSpacing(6);
    fShow->addRow(QString::fromUtf8("自动缩放"),
        combo("Fullscreen/autoFit", {QString::fromUtf8("上次使用过的"), QString::fromUtf8("不缩放"),
            QString::fromUtf8("适应窗口"), QString::fromUtf8("适应窗口大小 (仅小图片)"),
            QString::fromUtf8("适应窗口大小 (仅大图片)"), QString::fromUtf8("适应窗口宽度"),
            QString::fromUtf8("适应窗口高度"), QString::fromUtf8("适应窗口宽或高")}, 2));
    fShow->addRow(chk("Fullscreen/showPlaybar", QString::fromUtf8("显示播放条"), true));
    fShow->addRow(chk("Fullscreen/showInfo", QString::fromUtf8("显示信息"), true));
    fShow->addRow(chk("Fullscreen/showScrollbar", QString::fromUtf8("显示滚动条"), false));
    fShow->addRow(chk("Fullscreen/showToolbar", QString::fromUtf8("显示工具栏"), false));
    fShow->addRow(chk("Fullscreen/hideCursor", QString::fromUtf8("隐藏鼠标箭头"), true));
    root->addWidget(group(QString::fromUtf8("显示"), fShow));

    // 分组"其他"
    auto* fMisc = new QFormLayout;
    fMisc->setVerticalSpacing(6);
    fMisc->addRow(QString::fromUtf8("背景色"),
                  colorPick("Fullscreen/backColor", "#000000"));
    fMisc->addRow(chk("Fullscreen/dualMonitor", QString::fromUtf8("双显示器:使用第二显示器"), false));
    fMisc->addRow(chk("Fullscreen/floatView", QString::fromUtf8("浮动视图(鼠标移动到屏幕顶侧或右侧时出现)"), true));
    root->addWidget(group(QString::fromUtf8("其他"), fMisc));
    return wrapTitled(QString::fromUtf8("全屏"), root);
}

QWidget* SettingsDialog::pageCache() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(6);
    form->addRow(chk("Cache/useCatalog", QString::fromUtf8("启用缓存目录"), true));
    form->addRow(chk("Cache/thumbInDB", QString::fromUtf8("允许缓存缩略图"), true));
    form->addRow(QString::fromUtf8("压缩"),
        combo("Cache/compression", {QString::fromUtf8("无"),
            QString::fromUtf8("无损 - ZIP 压缩"), QString::fromUtf8("有损高品质(JPEG)"),
            QString::fromUtf8("低品质(JPEG)"), QString::fromUtf8("低质量 - 高质量 (WebP)")}, 4));
    form->addRow(chk("Cache/maxCacheOn", QString::fromUtf8("缓存缩略图最大容量(MB)"), true));
    form->addRow(spin("Cache/maxCacheMB", 64, 10240, 500));
    form->addRow(QString::fromUtf8("数据库引擎的内存占用(MB)"),
        spin("Cache/dbCacheMB", 8, 8192, 64));
    form->addRow(chk("Cache/checkOnStartup", QString::fromUtf8("启动时检查缓存的完整性"), false));
    return wrapTitled(QString::fromUtf8("缓存数据库"), form);
}
