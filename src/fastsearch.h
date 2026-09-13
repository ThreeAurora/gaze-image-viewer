#pragma once
// ═══════════════════════════════════════════
// 文件极速搜索(#16,2026-09-05 用户令"仿 Everything 融合其优点"的核心一步)
//   原理与 Everything 同源:不遍历目录树,而是对每个 NTFS 卷直接读 MFT ——
//   FSCTL_ENUM_USN_DATA 一次 IOCTL 流把全卷文件记录(FRN/父FRN/文件名)倒出来,
//   几秒内建好内存索引;搜索=内存子串匹配,命中后沿父链回溯拼出完整路径。
//   局限(如实):只索引本地 NTFS 卷;索引不持久化、随进程重建;FAT/exFAT/
//   网络盘不进来;搜索按文件名包含匹配,暂无通配/拼音/内容搜索(后续再融)。
//   后台线程只碰 Win32 API 与本线程数据,进度经 QMetaObject 回 GUI。
// ═══════════════════════════════════════════
#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QComboBox>       // #251 引擎切换下拉(Everything/内置 NTFS)
#include <QPushButton>
#include <QThreadPool>
#include <QPointer>
#include <QMetaObject>
#include <QMenu>
#include <QTimer>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <functional>
#include <vector>
#include "i18n.h"
#include "logger.h"
#include "everything_engine.h"   // #251:全盘快搜的 Everything 引擎分支(不可用自动回退内置)

#include <QCoreApplication>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>   // USN_MFT_ENUM_DATA / FSCTL_ENUM_USN_DATA

namespace {

// 一卷的索引:FRN → 记录(父FRN/文件名/是否目录)。目录也收(拼路径要用),
// 搜索只出文件。
struct FsRec {
    quint64 parent = 0;
    QString name;
    bool isDir = false;
};
struct FsVolIndex {
    QString drive;                              // "G"
    QHash<quint64, FsRec> entries;              // FRN → 记录
};

// MinGW 的 USN_RECORD_V2 带版本守卫(_WIN32_WINNT),这里按 MSDN 布局
// 自备一份同名结构,免受工程全局的版本宏牵连(布局逐字段对齐官方头)
struct UsnRecV2 {
    DWORD     RecordLength;
    WORD      MajorVersion;
    WORD      MinorVersion;
    DWORDLONG FileReferenceNumber;
    DWORDLONG ParentFileReferenceNumber;
    DWORDLONG Usn;
    LARGE_INTEGER TimeStamp;
    DWORD     Reason;
    DWORD     SourceInfo;
    DWORD     SecurityId;
    DWORD     FileAttributes;
    WORD      FileNameLength;
    WORD      FileNameOffset;
    WCHAR     FileName[1];
};

// 单卷枚举(阻塞式,只在池线程调用)。返回 false = 非 NTFS 或读不了。
bool enumerateVolumeUsn(const QString& driveLetter, FsVolIndex& out,
                        const std::function<void(qint64)>& progress) {
    wchar_t root[4] = {};
    root[0] = wchar_t(driveLetter.at(0).toLatin1());
    root[1] = L':';
    root[2] = L'\\';
    wchar_t fsName[MAX_PATH + 1] = {};
    DWORD fsFlags = 0;
    if (!GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr,
                               &fsFlags, fsName, MAX_PATH))
        return false;
    const QString fs = QString::fromWCharArray(fsName);
    if (!fs.startsWith(QLatin1String("NTFS"), Qt::CaseInsensitive))
        return false;

    const QString path = QStringLiteral("\\\\.\\%1:").arg(driveLetter);
    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()),
                           GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    // MinGW 头里 USN_MFT_ENUM_DATA 有版本守卫;FSCTL_ENUM_USN_DATA 的输入
    // 结构就是 MFT_ENUM_DATA(布局同 V1),用它不带版本坑
    MFT_ENUM_DATA med = {};
    med.StartFileReferenceNumber = 0;
    static const DWORD kBufSize = 1 << 20;   // 1MB 流水:IO 次数少,内存可控
    QByteArray buf(kBufSize, Qt::Uninitialized);
    DWORD got = 0;
    qint64 seen = 0;
    while (DeviceIoControl(h, FSCTL_ENUM_USN_DATA, &med, sizeof(med),
                           buf.data(), kBufSize, &got, nullptr)) {
        if (got <= sizeof(DWORDLONG)) break;
        const DWORDLONG next = *reinterpret_cast<DWORDLONG*>(buf.data());
        DWORD off = sizeof(DWORDLONG);
        while (off + sizeof(UsnRecV2) <= got) {
            auto* rec = reinterpret_cast<UsnRecV2*>(buf.data() + off);
            if (rec->RecordLength == 0) break;
            // FileNameOffset 是相对记录起始的偏移,要加上本记录在缓冲区的 off
            const QString name = QString::fromWCharArray(
                reinterpret_cast<const wchar_t*>(buf.data() + off + rec->FileNameOffset),
                rec->FileNameLength / 2);
            if (!name.isEmpty()) {
                FsRec r;
                r.parent = rec->ParentFileReferenceNumber;
                r.name = name;
                r.isDir = (rec->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                out.entries.insert(rec->FileReferenceNumber, r);
            }
            ++seen;
            off += rec->RecordLength;
        }
        if (progress) progress(seen);
        med.StartFileReferenceNumber = next;
    }
    CloseHandle(h);
    Logger::event(QStringLiteral("fastsearch: volume %1 indexed %2 entries")
                      .arg(driveLetter).arg(out.entries.size()));
    return !out.entries.isEmpty();
}

} // namespace

class FastSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit FastSearchDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(gazeTr("文件极速搜索 (NTFS 全盘索引)"));
        resize(880, 620);
        setObjectName(QStringLiteral("fastSearchDialog"));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(12, 12, 12, 10);
        root->setSpacing(8);

        auto* top = new QHBoxLayout;
        m_input = new QLineEdit;
        m_input->setPlaceholderText(gazeTr(
            "输入关键词回车搜索;Everything 引擎支持语法: 空格 与 | 或 ! 非 "
            "ext:jpg size:>1mb dm:today 通配符 *.?"));
        // #251 引擎切换下拉:默认 Everything(毫秒级);不可用时自动落回内置 NTFS
        // 索引。Everything 语法是超集,换过去搜索能力只升不降。
        m_engineCombo = new QComboBox;
        m_engineCombo->addItem(gazeTr("Everything 引擎"));
        m_engineCombo->addItem(gazeTr("内置 NTFS 索引"));
        m_countLabel = new QLabel(gazeTr("索引:未建"));
        top->addWidget(m_input, 1);
        top->addWidget(m_engineCombo);
        top->addWidget(m_countLabel);
        root->addLayout(top);

        m_status = new QLabel(gazeTr("正在启动 Everything 索引引擎…"));
        m_status->setObjectName(QStringLiteral("imgSearchStatus"));
        m_status->setWordWrap(true);
        root->addWidget(m_status);

        m_list = new QListWidget;
        m_list->setUniformItemSizes(true);
        m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_list->setContextMenuPolicy(Qt::CustomContextMenu);
        root->addWidget(m_list, 1);

        connect(m_input, &QLineEdit::returnPressed, this, [this]() { doSearch(); });
        connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
            revealResult(it->text());
        });
        connect(m_list, &QListWidget::customContextMenuRequested, this,
                [this](const QPoint& pos) {
            QListWidgetItem* it = m_list->itemAt(pos);
            if (!it) return;
            QMenu menu(this);
            QAction* aReveal = menu.addAction(gazeTr("在 Gaze 中定位"));
            QAction* aShow   = menu.addAction(gazeTr("在资源管理器中显示"));
            QAction* aCopy   = menu.addAction(gazeTr("复制完整路径"));
            QAction* chosen  = menu.exec(m_list->mapToGlobal(pos));
            if (chosen == aReveal) revealResult(it->text());
            else if (chosen == aShow)
                QProcess::startDetached("explorer",
                    { "/select,", QDir::toNativeSeparators(it->text()) });
            else if (chosen == aCopy)
                QApplication::clipboard()->setText(QDir::toNativeSeparators(it->text()));
        });

        // 拉起即:① 后台建内置 NTFS 索引(Everything 掉线时的兜底,双引擎并行);
        // ② 异步拉起 Everything 独立实例,就绪后引擎下拉转正。
        QTimer::singleShot(0, this, [this]() {
            startIndexing();
            ensureEverythingEngine();
        });
    }

private:
    void startIndexing() {
        if (m_scanning) return;
        m_scanning = true;
        QStringList drives;
        const DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i)
            if (mask & (1u << i)) {
                const QString letter = QString(QChar('A' + i));
                const UINT type = GetDriveTypeW(
                    reinterpret_cast<const wchar_t*>((letter + ":\\").utf16()));
                if (type == DRIVE_FIXED) drives << letter;   // 移动介质/网络盘不进索引
            }
        QPointer<FastSearchDialog> self(this);
        QThreadPool::globalInstance()->start([self, drives]() {
            for (const QString& d : drives) {
                FsVolIndex vol;
                const bool ok = enumerateVolumeUsn(d, vol, [self](qint64 n) {
                    QMetaObject::invokeMethod(self, [self, n]() {
                        if (self) self->m_status->setText(
                            gazeTr("索引中… 已读 %1 条记录").arg(n));
                    }, Qt::QueuedConnection);
                });
                QMetaObject::invokeMethod(self, [self, ok, vol = std::move(vol)]() {
                    if (!self) return;
                    if (ok) {
                        self->m_vols.push_back(std::move(vol));
                        qint64 total = 0;
                        for (const auto& v : self->m_vols) total += v.entries.size();
                        self->m_countLabel->setText(gazeTr("索引:%1 条").arg(total));
                    }
                }, Qt::QueuedConnection);
            }
            QMetaObject::invokeMethod(self, [self]() {
                if (!self) return;
                self->m_scanning = false;
                self->m_status->setText(gazeTr("索引就绪,输入关键词回车搜索"));
            }, Qt::QueuedConnection);
        });
    }

    // 沿父链回溯拼完整路径;卷根(FRN 5)即止
    QString fullPathOf(const FsVolIndex& vol, quint64 frn) const {
        QStringList parts;
        quint64 cur = frn;
        int guard = 0;
        while (guard++ < 64) {
            const auto it = vol.entries.constFind(cur);
            if (it == vol.entries.constEnd()) break;
            if (cur == it->parent) break;                // 卷根:parent==自身
            parts.prepend(it->name);
            if (it->parent == 5) break;                  // 直接挂在根下
            cur = it->parent;
        }
        return vol.drive + QLatin1String(":/") + parts.join(QLatin1Char('/'));
    }

    // 引擎就绪前悬着:Everything 出来前只在状态栏说明状态;失败则落回内置
    void ensureEverythingEngine() {
        QPointer<FastSearchDialog> self(this);
        ev_impl::ensureRunning(self, [self](bool ok) {
            if (!self) return;
            if (ok) {
                self->m_evReady = true;
                self->m_status->setText(gazeTr("Everything 引擎就绪,输入关键词回车搜索"));
            } else {
                self->m_evFailed = true;
                self->m_engineCombo->setCurrentIndex(1);   // 自动落回内置索引
                self->m_engineCombo->setToolTip(gazeTr("Everything 引擎不可用,已改用内置 NTFS 索引"));
                if (self->m_scanning)
                    self->m_status->setText(gazeTr("Everything 引擎不可用,正在用内置 NTFS 索引(构建中)…"));
                else
                    self->m_status->setText(gazeTr("Everything 引擎不可用,已改用内置 NTFS 索引"));
            }
        });
    }

    void doSearch() {
        const QString q = m_input->text().trimmed();
        m_list->clear();
        if (q.isEmpty()) return;
        // #251 分流:下拉选了 Everything 且引擎就绪 → 走 Everything 毫秒查询;
        // 其余(引擎未就绪/已失败/用户切内置)一律走原有 USN 索引
        if (m_engineCombo->currentIndex() == 0) {
            if (m_evReady) { searchEverything(q); return; }
            if (m_evFailed) {
                m_status->setText(gazeTr("Everything 引擎不可用,已用内置 NTFS 索引搜索"));
            } else {
                m_status->setText(gazeTr("Everything 引擎启动中,已先走内置 NTFS 索引"));
            }
        }
        searchUsn(q);
    }

    void searchEverything(const QString& q) {
        m_status->setText(gazeTr("Everything 搜索中…"));
        QPointer<FastSearchDialog> self(this);
        const int limit = 500;
        ev_impl::searchFilesAsync(q, limit, this, [self, q](bool ok, const QStringList& paths) {
            if (!self) return;
            if (!ok) {
                // 实例竟在查询时掉线:明确告知并落回内置,别让用户干等
                self->m_evFailed = true;
                self->m_engineCombo->setCurrentIndex(1);
                self->m_status->setText(gazeTr("Everything 查询失败,已改用内置 NTFS 索引搜索"));
                self->searchUsn(q);
                return;
            }
            self->m_list->clear();
            for (const QString& p : paths) self->m_list->addItem(p);
            // 命中总数异步补报(状态栏可见"共命中 N 项")—— 返回慢也不拦结果展示
            ev_impl::countAsync(q, self, [self, shown = paths.size()](bool okc, qint64 n) {
                if (!self) return;
                const auto base = gazeTr("命中 %1 项%2(双击在 Gaze 打开)");
                if (okc && n > shown)
                    self->m_status->setText(base.arg(n).arg(
                        gazeTr(",已显示前 %1 ").arg(shown)));
                else
                    self->m_status->setText(base.arg(shown).arg(QString()));
            });
        });
    }

    void searchUsn(const QString& q) {
        m_list->clear();
        qint64 total = 0;
        int shown = 0;
        for (const FsVolIndex& vol : m_vols) {
            for (auto it = vol.entries.constBegin(); it != vol.entries.constEnd(); ++it) {
                if (it->isDir) continue;   // 目录不是搜索目标(拼路径仍会用它们)
                if (!it->name.contains(q, Qt::CaseInsensitive)) continue;
                ++total;
                if (shown < 500) {
                    m_list->addItem(fullPathOf(vol, it.key()));
                    ++shown;
                }
            }
        }
        m_status->setText(shown >= 500
            ? gazeTr("命中至少 %1 项,已显示前 500(双击在 Gaze 打开)").arg(total)
            : gazeTr("命中 %1 项(双击在 Gaze 打开)").arg(total));
    }

    void revealResult(const QString& path) {
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            QMessageBox::information(this, gazeTr("定位"),
                gazeTr("文件当前不存在(可能已被移动或删除):\n%1").arg(path));
            return;
        }
        QObject* mw = this;
        while (mw && mw->metaObject()->indexOfMethod("revealFile(QString)") < 0)
            mw = mw->parent();
        if (mw) {
            QMetaObject::invokeMethod(mw, "revealFile", Q_ARG(QString, path));
            accept();
        } else {
            QMessageBox::information(this, gazeTr("定位"), gazeTr("无法定位主窗口"));
        }
    }

    QLineEdit* m_input = nullptr;
    QComboBox* m_engineCombo = nullptr;   // #251 引擎切换:0=Everything 1=内置 NTFS
    QLabel* m_status = nullptr;
    QLabel* m_countLabel = nullptr;
    QListWidget* m_list = nullptr;
    std::vector<FsVolIndex> m_vols;   // 就绪的卷索引(GUI 线程持有)
    bool m_scanning = false;
    // #251 Everything 引擎就绪/失败(失败=自动落回内置,下拉置灰切换)
    bool m_evReady  = false;
    bool m_evFailed = false;
};
