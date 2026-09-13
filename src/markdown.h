#pragma once
// ═══════════════════════════════════════════
// 极简 Markdown → HTML 渲染器(#82)
//   自用预览,不追求 CommonMark 全兼容,目标是"以 MD 格式展示"读起来舒服:
//   标题 / 段落 / 粗斜体删除线 / 行内代码 / 围栏代码块 / 引用 / 有序无序列表 /
//   任务列表 / 链接 / 图片 / 分隔线 / 表格 / 自动链接 / 硬换行
//   刻意不引入第三方库:不给自己找许可证与 DLL 依赖的麻烦
//   已知限制(2026-08-31 实测确认):嵌套列表会被拉平成同级 <li>,不还原缩进层级;
//   表格只支持单层管道表。自用预览够用,若要全兼容再上 CommonMark 库
// ═══════════════════════════════════════════
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QFile>
#include "textlimit.h"
#include "theme.h"

namespace Md {

inline QString escapeHtml(const QString& s) {
    QString o = s;
    o.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    o.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    o.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return o;
}

// 行内代码手动替换:Qt 6.8 的 QString::replace(QRegularExpression, Functor)
// 在本工具链上不可用,这里自己走 globalMatch。
// 命中的代码块整段抽成占位符存进 store:代码里的 * _ ~~ 不再参与后续强调
// 规则(旧实现只转义不占位,`__init__` 会被斜体正则啃成错排)
inline QString wrapInlineCode(const QString& s, QStringList& store) {
    QRegularExpression re(QStringLiteral("`([^`]+)`"));
    QString out;
    int last = 0;
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        out += QStringLiteral("\x01%1\x01").arg(store.size());
        store << QStringLiteral("<code>") + escapeHtml(m.captured(1))
              + QStringLiteral("</code>");
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out;
}

// 行内:先抽行内代码占位(避免代码里的 * _ 被当强调),再按顺序应用强调规则,
// 最后回填代码段
inline QString inlineMd(QString s) {
    QStringList codeStore;
    // 行内代码 `x`
    s = wrapInlineCode(s, codeStore);
    // 图片 ![alt](src) —— 摆在链接之前,否则会被链接规则吃掉
    s.replace(QRegularExpression(QStringLiteral("!\\[([^\\]]*)\\]\\(([^\\s)]+)(?:\\s+\"([^\"]*)\")?\\)")),
              QStringLiteral(R"(<img src="\2" alt="\1" title="\3"/>)"));
    // 链接 [text](url)
    s.replace(QRegularExpression(QStringLiteral("\\[([^\\]]+)\\]\\(([^\\s)]+)\\)")),
              QStringLiteral(R"(<a href="\2">\1</a>)"));
    // 自动链接 <http://...>
    s.replace(QRegularExpression(QStringLiteral("<(https?://[^>]+)>")),
              QStringLiteral(R"(<a href="\1">\1</a>)"));
    // 裸 URL(不在引号/标签内的)
    s.replace(QRegularExpression(QStringLiteral("(?<![\"'(>])((?:https?|ftp)://[^\\s<>\"]+)")),
              QStringLiteral(R"(<a href="\1">\1</a>)"));
    // 强调:***x*** → 粗斜体,**x** → 粗,*x*/_x_ → 斜,~~x~~ → 删除线
    s.replace(QRegularExpression(QStringLiteral("\\*\\*\\*(.+?)\\*\\*\\*")),
              QStringLiteral("<b><i>\\1</i></b>"));
    s.replace(QRegularExpression(QStringLiteral("\\*\\*(.+?)\\*\\*")),
              QStringLiteral("<b>\\1</b>"));
    s.replace(QRegularExpression(QStringLiteral("(?<![\\w*])\\*(?!\\s)(.+?)(?<!\\s)\\*(?!\\w)")),
              QStringLiteral("<i>\\1</i>"));
    s.replace(QRegularExpression(QStringLiteral("(?<![\\w_])_(?!\\s)(.+?)(?<!\\s)_(?!\\w)")),
              QStringLiteral("<i>\\1</i>"));
    s.replace(QRegularExpression(QStringLiteral("~~(.+?)~~")),
              QStringLiteral("<s>\\1</s>"));
    // 回填行内代码段(占位符字符不会出现在上述任何规则的匹配里)
    for (int i = 0; i < codeStore.size(); ++i)
        s.replace(QStringLiteral("\x01%1\x01").arg(i), codeStore.at(i));
    return s;
}

// 判断一行是否"打断段落"的块级起始
inline bool isBlockStart(const QString& line) {
    if (line.isEmpty()) return true;
    const QRegularExpression re(QStringLiteral(
        R"(^(#{1,6}\s|\s{0,3}>|\s*([-*+]|\d+[.)])\s|\s{0,3}(```|~~~)|\s{0,3}(---+|\*\*\*+|___+)\s*$|\s*\|))"));
    return re.match(line).hasMatch();
}

inline QString render(const QString& src) {
    const QStringList lines = src.split(QLatin1Char('\n'));
        QString html;
    html.reserve(src.size() * 2);

    QStringList para;      // 累积中的段落行
    auto flushPara = [&]() {
        if (para.isEmpty()) return;
        html += QStringLiteral("<p>") + inlineMd(para.join(QStringLiteral("<br/>")))
              + QStringLiteral("</p>\n");
        para.clear();
    };

    bool inCode = false, inList = false;
    QString codeFence, codeLang;
    QString listTag;
    int i = 0;

    auto closeList = [&]() {
        if (inList) { html += QStringLiteral("</") + listTag + QStringLiteral(">\n"); inList = false; }
    };

    while (i < lines.size()) {
        const QString raw = lines[i];
        const QString line = raw;
        const QString t = line.trimmed();

        // ── 围栏代码块 ──
        QRegularExpressionMatch fence = QRegularExpression(
            QStringLiteral("^\\s{0,3}(```|~~~)\\s*([\\w+-]*)\\s*$")).match(line);
        if (fence.hasMatch()) {
            if (!inCode) {
                flushPara(); closeList();
                inCode = true; codeFence = fence.captured(1); codeLang = fence.captured(2);
                html += QStringLiteral("<pre><code");
                if (!codeLang.isEmpty()) html += QStringLiteral(" class=\"%1\"").arg(codeLang);
                html += QStringLiteral(">");
            } else if (fence.captured(1) == codeFence) {
                inCode = false;
                html += QStringLiteral("</code></pre>\n");
            } else {
                html += escapeHtml(line) + QLatin1Char('\n');
            }
            ++i; continue;
        }
        if (inCode) { html += escapeHtml(line) + QLatin1Char('\n'); ++i; continue; }

        // ── 空行:结束段落与列表 ──
        if (t.isEmpty()) { flushPara(); closeList(); ++i; continue; }

        // ── 标题 ──
        QRegularExpressionMatch h = QRegularExpression(
            QStringLiteral("^(#{1,6})\\s+(.*?)\\s*#*\\s*$")).match(t);
        if (h.hasMatch()) {
            flushPara(); closeList();
            const int lv = h.captured(1).length();
            html += QStringLiteral("<h%1>%2</h%1>\n").arg(lv).arg(inlineMd(h.captured(2)));
            ++i; continue;
        }

        // ── 分隔线 ──
        if (QRegularExpression(QStringLiteral("^\\s{0,3}(-{3,}|\\*{3,}|_{3,})\\s*$")).match(line).hasMatch()) {
            flushPara(); closeList();
            html += QStringLiteral("<hr/>\n");
            ++i; continue;
        }

        // ── 表格:当前行含 | 且下一行是分隔行 ──
        if (t.contains(QLatin1Char('|')) && i + 1 < lines.size()
            && QRegularExpression(QStringLiteral("^\\s*\\|?\\s*[:\\-]+\\s*(\\|\\s*[:\\-]+\\s*)+\\|?\\s*$"))
                   .match(lines[i + 1]).hasMatch()) {
            flushPara(); closeList();
            auto cells = [](const QString& row) {
                QString r = row.trimmed();
                if (r.startsWith(QLatin1Char('|'))) r.remove(0, 1);
                if (r.endsWith(QLatin1Char('|'))) r.chop(1);
                return r.split(QLatin1Char('|'));
            };
            const QStringList head = cells(t);
            ++i; ++i;   // 跳过表头与分隔行
            html += QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">\n<thead><tr>");
            for (const QString& c : head)
                html += QStringLiteral("<th>%1</th>").arg(inlineMd(c.trimmed()));
            html += QStringLiteral("</tr></thead>\n<tbody>\n");
            while (i < lines.size() && lines[i].trimmed().contains(QLatin1Char('|'))) {
                const QStringList row = cells(lines[i]);
                html += QStringLiteral("<tr>");
                for (const QString& c : row)
                    html += QStringLiteral("<td>%1</td>").arg(inlineMd(c.trimmed()));
                html += QStringLiteral("</tr>\n");
                ++i;
            }
            html += QStringLiteral("</tbody></table>\n");
            continue;
        }

        // ── 引用 ──
        if (t.startsWith(QLatin1Char('>'))) {
            flushPara(); closeList();
            QString quote;
            while (i < lines.size() && lines[i].trimmed().startsWith(QLatin1Char('>'))) {
                QString q = lines[i].trimmed().mid(1);
                if (q.startsWith(QLatin1Char(' '))) q.remove(0, 1);
                quote += q + QLatin1Char('\n');
                ++i;
            }
            html += QStringLiteral("<blockquote>%1</blockquote>\n")
                        .arg(inlineMd(quote.trimmed()));
            continue;
        }

        // ── 列表(有序/无序/任务) ──
        QRegularExpressionMatch li = QRegularExpression(
            QStringLiteral("^\\s*([-*+]|(\\d+)[.)])\\s+(.*)$")).match(line);
        if (li.hasMatch()) {
            flushPara();
            const bool ordered = !li.captured(2).isEmpty();
            const QString want = ordered ? QStringLiteral("ol") : QStringLiteral("ul");
            if (!inList || listTag != want) { closeList(); listTag = want;
                html += QStringLiteral("<%1>\n").arg(want); inList = true; }
            QString item = li.captured(3);
            QRegularExpressionMatch task = QRegularExpression(
                QStringLiteral("^\\[([ xX])\\]\\s+(.*)$")).match(item);
            if (task.hasMatch()) {
                const bool done = task.captured(1).trimmed().isEmpty() == false
                                  && task.captured(1) != QLatin1String(" ");
                html += QStringLiteral("<li><input type=\"checkbox\" disabled%1/> %2</li>\n")
                            .arg(done ? QStringLiteral(" checked") : QString())
                            .arg(inlineMd(task.captured(2)));
            } else {
                html += QStringLiteral("<li>%1</li>\n").arg(inlineMd(item));
            }
            ++i; continue;
        }

        // ── 普通段落行 ──
        if (!inList && isBlockStart(line)) closeList();
        para << t;
        ++i;
    }
    flushPara();
    closeList();
    if (inCode) html += QStringLiteral("</code></pre>\n");

    // 包一层带样式的壳(#248:阅读面随主题。浅色=白底黑字的标准阅读面;
    // 深色逐字保留原已验收暗底。旧注释「浅色下 MD 仍是暗底」已按用户
    // 2026-09-04 纯黑清零令作废)。MD 每次渲染重新生成 CSS,无缓存即无刷新钩子
    const QString css = QStringLiteral(
        "body{font-family:'Microsoft YaHei','Segoe UI',sans-serif;font-size:13px;"
        "background:%1;color:%2;line-height:1.7;margin:0;padding:0;}"
        "h1,h2,h3,h4,h5,h6{color:%3;margin:18px 0 8px;line-height:1.35;}"
        "h1{font-size:22px;border-bottom:1px solid %4;padding-bottom:6px;}"
        "h2{font-size:19px;border-bottom:1px solid %4;padding-bottom:5px;}"
        "h3{font-size:16px;} h4{font-size:14px;} h5,h6{font-size:13px;color:%5;}"
        "p{margin:8px 0;}"
        "a{color:%6;text-decoration:none;} a:hover{text-decoration:underline;}"
        "code{background:%7;color:%8;padding:1px 5px;border-radius:3px;"
        "font-family:'Consolas','Courier New',monospace;font-size:12px;}"
        "pre{background:%9;border:1px solid %10;border-radius:5px;"
        "padding:10px 12px;overflow:auto;margin:10px 0;}"
        "pre code{background:none;color:%11;padding:0;font-size:12px;}"
        "blockquote{border-left:3px solid %12;background:%13;margin:10px 0;"
        "padding:6px 12px;color:%14;}"
        "ul,ol{margin:8px 0;padding-left:24px;}"
        "li{margin:3px 0;}"
        "table{border-color:%4;margin:10px 0;font-size:12px;}"
        "th{background:%7;color:%3;} td,th{padding:5px 10px;}"
        "hr{border:none;border-top:1px solid %4;margin:16px 0;}"
        "img{max-width:100%;}")
        .arg(Theme::T("#000000", "#FFFFFF"))   // %1 页底
        .arg(Theme::T("#E0E0E0", "#1F1F26"))   // %2 正文
        .arg(Theme::T("#FFFFFF", "#1F1F26"))   // %3 标题/表头文字
        .arg(Theme::T("#3A3A42", "#D9D9E0"))   // %4 分隔线/表格框
        .arg(Theme::T("#C8C8CE", "#44444C"))   // %5 h5/h6
        .arg(Theme::T("#6BA6F5", "#2F6FE0"))   // %6 链接
        .arg(Theme::T("#2A2A31", "#F2F2F5"))   // %7 行内代码底/表头底
        .arg(Theme::T("#F0C674", "#9A6B00"))   // %8 行内代码字
        .arg(Theme::T("#1C1C22", "#F6F6F8"))   // %9 代码块底
        .arg(Theme::T("#34343C", "#D9D9E0"))   // %10 代码块框
        .arg(Theme::T("#D8D8DC", "#33333B"))   // %11 代码块字
        .arg(Theme::T("#5A5A66", "#B6B6BF"))   // %12 引用条
        .arg(Theme::T("#232329", "#F3F3F5"))   // %13 引用底
        .arg(Theme::T("#B8B8C0", "#55555E"));  // %14 引用字
    // 双参一次替换:正文含 %N 字样(用户文档里写占位符)也不会被误替换
    return QStringLiteral(
        "<html><head><meta charset=\"utf-8\"><style>%1</style></head><body>%2"
        "</body></html>").arg(css, html);
}

inline QString renderFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    // #111:与 txt 预览同一套上限(字节/行数/每行字符数)。渲染成 HTML 也逃不开
    // QTextEdit 的排版代价 —— 围栏代码块里一个 512KB 的无空格长行同样能冻死界面。
    bool byteCut = false;
    qint64 total = 0;
    const QString head = TextCut::readHead(f, &byteCut, &total);
    f.close();
    const TextCut::Clip c = TextCut::clip(head, byteCut, total);
    QString src = c.text;
    const QString note = TextCut::noticeOf(c);
    if (!note.isEmpty()) src += QStringLiteral("\n\n> ") + note + QStringLiteral("\n");
    return render(src);
}

} // namespace Md
