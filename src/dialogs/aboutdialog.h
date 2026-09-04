#pragma once
#include "constants.h"
#include "i18n.h"

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QFont>
#include <QFrame>

// 关于对话框:致敬 XnView MP + 开源致谢 + 版本。非模态,调用方 show() +
// finished→deleteLater(与其余常驻工具窗同一寿命协议)。随包组件的许可逐项
// 实测核实过(ffmpeg -version 无 --enable-gpl 即 LGPL 构建、LibRaw 头文件、
// gs --version);行内一律不写死随包组件版本号(会随 vendor 更新烂掉),
// Qt 版本走运行时 qVersion(),Gaze 版本走 constants.h 的 GAZE_VERSION。
class AboutDialog : public QDialog {
public:
    explicit AboutDialog(QWidget* parent) : QDialog(parent) {
        setWindowTitle(gazeTr("关于 Gaze"));
        setFixedWidth(580);
        setStyleSheet(QString::fromUtf8(
            "QDialog{background:%1;}"
            "QLabel{color:%2;background:transparent;}"
            "QFrame{background:%3;max-height:1px;border:none;}"
            "QPushButton{background:%4;color:%2;border:1px solid %3;"
            "padding:5px 18px;border-radius:4px;}"
            "QPushButton:hover{border-color:%5;}")
            .arg(C_CONTENT, C_TEXT, C_SEPARATOR, C_TOOLBAR, C_ACCENT));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(24, 20, 24, 16);
        root->setSpacing(10);

        auto* head = new QHBoxLayout;
        auto* icon = new QLabel;
        icon->setPixmap(QPixmap(":/Gaze.png").scaled(
            48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        head->addWidget(icon, 0, Qt::AlignTop);
        auto* headText = new QVBoxLayout;
        headText->setSpacing(2);
        auto* name = new QLabel(QStringLiteral("Gaze"));
        QFont nameFont = name->font();
        nameFont.setPointSize(20);
        nameFont.setBold(true);
        name->setFont(nameFont);
        headText->addWidget(name);
        auto* ver = new QLabel(gazeTr("版本 %1 · C++17 + Qt %2")
                                   .arg(QString::fromLatin1(GAZE_VERSION),
                                        QString::fromLatin1(qVersion())));
        headText->addWidget(ver);
        auto* tagline = new QLabel(gazeTr(
            "本地媒体查看器与文件浏览器 —— 图片、视频、RAW、PDF，一个窗口全搞定。"));
        tagline->setWordWrap(true);
        headText->addWidget(tagline);
        head->addLayout(headText, 1);
        root->addLayout(head);

        root->addWidget(new QFrame);

        auto* tribute = new QLabel(gazeTr(
            "Gaze 的界面形态与交互对标 XnView MP —— 本项目受其启发，以现代化技术栈"
            "重新实现，旨在作为一款轻量的开源替代方案（a poor man's XnView MP）。"));
        tribute->setWordWrap(true);
        root->addWidget(tribute);

        auto* creditsTitle = new QLabel(gazeTr("开源致谢"));
        QFont titleFont = creditsTitle->font();
        titleFont.setBold(true);
        creditsTitle->setFont(titleFont);
        root->addWidget(creditsTitle);

        auto* credits = new QLabel(
            gazeTr("<b>Qt %1</b> — LGPL v3 · 界面框架").arg(QString::fromLatin1(qVersion())) + "<br>" +
            gazeTr("<b>FFmpeg</b> — LGPL v3 · 视频/音频解码、缩略图与动态照片提取") + "<br>" +
            gazeTr("<b>LibRaw</b> — LGPL-2.1 / CDDL · RAW 相机格式解码") + "<br>" +
            gazeTr("<b>Ghostscript</b> — AGPL · PDF 渲染") + "<br>" +
            gazeTr("<b>jpegtran</b>（libjpeg-turbo）— 类 BSD 许可 · 无损旋转/镜像"));
        credits->setTextFormat(Qt::RichText);
        credits->setWordWrap(true);
        root->addWidget(credits);

        root->addStretch(1);

        auto* btnRow = new QHBoxLayout;
        btnRow->addStretch(1);
        auto* close = new QPushButton(gazeTr("关闭"));
        connect(close, &QPushButton::clicked, this, &QDialog::close);
        btnRow->addWidget(close);
        root->addLayout(btnRow);
    }
};
