#pragma once
#include <QString>
#include <QVariantMap>
#include <optional>

namespace LivePhoto {

// 检测结果类型:
//   "companion" — 外部配对视频（Apple Live Photo）
//   "motion"    — JPEG 内嵌 MP4（Google Motion Photo / Samsung）
struct Info {
    QString  type;        // "companion" / "motion"
    QString  videoPath;   // 视频文件路径
    int64_t  videoOffset = -1;  // 内嵌视频在 JPEG 中的字节偏移（仅 embedded）
    int64_t  videoLength = 0;   // 内嵌视频长度（仅 embedded）
    bool     embedded   = false;
};

std::optional<Info> detect(const QString& imagePath);
std::optional<Info> detectCompanion(const QString& imagePath);
std::optional<Info> detectEmbedded(const QString& imagePath);

// 从 JPEG 中提取内嵌视频到临时文件（需要 ffmpeg 修复 MOOV atom）
QString extractEmbeddedVideo(const QString& imagePath, const Info& info);

} // namespace LivePhoto
