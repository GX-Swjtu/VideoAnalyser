#include "mediainfowidget.h"
#include "packetreader.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFileInfo>
#include <QtMath>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/channel_layout.h>
}

MediaInfoWidget::MediaInfoWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setIndentation(20);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    layout->addWidget(m_tree);
}

void MediaInfoWidget::setMediaInfo(const QString &filePath,
                                   const QVector<StreamInfo> &streams,
                                   AVFormatContext *fmtCtx,
                                   const QVector<PacketInfo> &packets)
{
    clear();

    if (!fmtCtx) return;

    QFileInfo fi(filePath);

    // ===== Summary =====
    auto *summary = addSection(QStringLiteral("Summary"), QColor(0, 120, 0));
    addField(summary, QStringLiteral("File name"), fi.absoluteFilePath());
    addField(summary, QStringLiteral("File size"), formatFileSize(fi.size()));
    addField(summary, QStringLiteral("Bit rate"), formatBitrate(fmtCtx->bit_rate));
    double durSec = (fmtCtx->duration != AV_NOPTS_VALUE)
                        ? fmtCtx->duration / static_cast<double>(AV_TIME_BASE)
                        : 0.0;
    addField(summary, QStringLiteral("Duration"), formatDuration(durSec));

    // ===== Details =====
    auto *details = addSection(QStringLiteral("Details"), QColor(0, 100, 180));
    addField(details, QStringLiteral("Format"), QString::fromUtf8(fmtCtx->iformat ? fmtCtx->iformat->long_name : "unknown"));

    int videoCount = 0, audioCount = 0, subtitleCount = 0;
    for (const auto &s : streams) {
        if (s.mediaType == AVMEDIA_TYPE_VIDEO) videoCount++;
        else if (s.mediaType == AVMEDIA_TYPE_AUDIO) audioCount++;
        else if (s.mediaType == AVMEDIA_TYPE_SUBTITLE) subtitleCount++;
    }
    addField(details, QStringLiteral("ES count"), QString::number(streams.size()));
    addField(details, QStringLiteral("Video ES count"), QStringLiteral("%1 Video ES").arg(videoCount));
    addField(details, QStringLiteral("Audio ES count"), QStringLiteral("%1 Audio ES").arg(audioCount));
    if (subtitleCount > 0)
        addField(details, QStringLiteral("Subtitle ES count"), QStringLiteral("%1 Subtitle ES").arg(subtitleCount));

    // ===== 每个视频流 =====
    int videoIndex = 0;
    for (const auto &s : streams) {
        if (s.mediaType != AVMEDIA_TYPE_VIDEO) continue;
        videoIndex++;

        auto *section = addSection(QStringLiteral("Video [%1]").arg(videoIndex), QColor(0, 120, 0));
        if (!s.codecpar) continue;

        // 编解码器名称
        const AVCodecDescriptor *desc = avcodec_descriptor_get(s.codecpar->codec_id);
        QString codecLongName = desc ? QString::fromUtf8(desc->long_name) : s.codecName;
        addField(section, QStringLiteral("Format codec"), codecLongName);

        // Profile@Level
        const char *profileName = avcodec_profile_name(s.codecpar->codec_id, s.codecpar->profile);
        QString profileStr = profileName ? QString::fromUtf8(profileName) : QStringLiteral("unknown");
        if (s.codecpar->level >= 0) {
            // H.264/H.265 level 通常为整数×10，如 40 表示 4.0
            double levelVal = s.codecpar->level / 10.0;
            if (s.codecpar->codec_id == AV_CODEC_ID_H264 ||
                s.codecpar->codec_id == AV_CODEC_ID_HEVC) {
                profileStr += QStringLiteral("@L%1").arg(levelVal, 0, 'f', 1);
            } else {
                profileStr += QStringLiteral("@L%1").arg(s.codecpar->level);
            }
        }
        addField(section, QStringLiteral("Format profile@level"), profileStr);

        // 分辨率
        addField(section, QStringLiteral("Display resolution"),
                 QStringLiteral("%1 x %2").arg(s.codecpar->width).arg(s.codecpar->height));

        // 像素格式
        auto pixFmt = static_cast<AVPixelFormat>(s.codecpar->format);
        const char *pixFmtName = av_get_pix_fmt_name(pixFmt);
        addField(section, QStringLiteral("Video pixel format"),
                 pixFmtName ? QString::fromUtf8(pixFmtName) : QStringLiteral("unknown"));

        // 位深
        const AVPixFmtDescriptor *pixDesc = av_pix_fmt_desc_get(pixFmt);
        if (pixDesc && pixDesc->nb_components > 0) {
            addField(section, QStringLiteral("Pixel bit depth"),
                     QStringLiteral("%1 bit").arg(pixDesc->comp[0].depth));
        }

        // 帧率
        if (s.avgFrameRate.den > 0) {
            double fps = av_q2d(s.avgFrameRate);
            addField(section, QStringLiteral("Frame rate"),
                     QStringLiteral("%1 fps").arg(fps, 0, 'f', 4));
        }

        // GOP 大小
        double gopAvg = computeAverageGopSize(packets, s.index);
        if (gopAvg > 0) {
            addField(section, QStringLiteral("GOP size"), QString::number(qRound(gopAvg)));
        }

        // 场序
        QString scanType;
        switch (s.codecpar->field_order) {
        case AV_FIELD_PROGRESSIVE: scanType = QStringLiteral("Progressive"); break;
        case AV_FIELD_TT:
        case AV_FIELD_BB:
        case AV_FIELD_TB:
        case AV_FIELD_BT: scanType = QStringLiteral("Interlaced"); break;
        default: scanType = QStringLiteral("Unknown"); break;
        }
        addField(section, QStringLiteral("Scan type"), scanType);

        // SAR / DAR
        AVRational sar = s.codecpar->sample_aspect_ratio;
        if (sar.num > 0 && sar.den > 0) {
            addField(section, QStringLiteral("Sample aspect ratio"),
                     formatAspectRatio(sar.num, sar.den) + QStringLiteral(" (SAR)"));
            // DAR = width*SAR / height
            int darNum = s.codecpar->width * sar.num;
            int darDen = s.codecpar->height * sar.den;
            addField(section, QStringLiteral("Display aspect ratio"),
                     formatAspectRatio(darNum, darDen) + QStringLiteral(" (DAR)"));
        }

        // 时间基
        addField(section, QStringLiteral("Time base"),
                 QStringLiteral("%1 / %2").arg(s.timeBase.num).arg(s.timeBase.den));
    }

    // ===== 每个音频流 =====
    int audioIndex = 0;
    for (const auto &s : streams) {
        if (s.mediaType != AVMEDIA_TYPE_AUDIO) continue;
        audioIndex++;

        auto *section = addSection(QStringLiteral("Audio [%1]").arg(audioIndex), QColor(200, 50, 50));
        if (!s.codecpar) continue;

        // 编解码器
        const AVCodecDescriptor *desc = avcodec_descriptor_get(s.codecpar->codec_id);
        QString codecLongName = desc ? QString::fromUtf8(desc->long_name) : s.codecName;
        addField(section, QStringLiteral("Format codec"), codecLongName);

        // Profile
        const char *profileName = avcodec_profile_name(s.codecpar->codec_id, s.codecpar->profile);
        if (profileName)
            addField(section, QStringLiteral("Format profile@level"), QString::fromUtf8(profileName));

        // 时长
        if (s.duration > 0 && s.timeBase.den > 0) {
            double streamDur = s.duration * av_q2d(s.timeBase);
            addField(section, QStringLiteral("Duration"), formatDuration(streamDur));
        }

        // 采样率
        addField(section, QStringLiteral("Sample rate"), QStringLiteral("%1 Hz").arg(s.sampleRate));

        // 位深
        auto sampleFmt = static_cast<AVSampleFormat>(s.codecpar->format);
        int bitsPerSample = av_get_bytes_per_sample(sampleFmt) * 8;
        QString fmtName = QString::fromUtf8(av_get_sample_fmt_name(sampleFmt));
        addField(section, QStringLiteral("Sample bits"),
                 QStringLiteral("%1 bit (%2)").arg(bitsPerSample).arg(fmtName));

        // 声道数
        addField(section, QStringLiteral("Channel number"),
                 QStringLiteral("%1 channels").arg(s.channels));

        // 声道布局
        char layoutBuf[128] = {};
        av_channel_layout_describe(&s.codecpar->ch_layout, layoutBuf, sizeof(layoutBuf));
        addField(section, QStringLiteral("Channel layout"), QString::fromUtf8(layoutBuf));

        // 码率
        if (s.codecpar->bit_rate > 0)
            addField(section, QStringLiteral("Average bit rate"), formatBitrate(s.codecpar->bit_rate));

        // 语言
        if (!s.language.isEmpty())
            addField(section, QStringLiteral("Language"), s.language);
    }

    // ===== 每个字幕流 =====
    int subIndex = 0;
    for (const auto &s : streams) {
        if (s.mediaType != AVMEDIA_TYPE_SUBTITLE) continue;
        subIndex++;

        auto *section = addSection(QStringLiteral("Subtitle [%1]").arg(subIndex), QColor(128, 128, 0));
        addField(section, QStringLiteral("Format codec"), s.codecName);
        if (!s.language.isEmpty())
            addField(section, QStringLiteral("Language"), s.language);
    }

    m_tree->expandAll();
}

void MediaInfoWidget::clear()
{
    m_tree->clear();
}

// ===== 静态工具方法 =====

QString MediaInfoWidget::formatFileSize(int64_t bytes)
{
    double mb = bytes / (1024.0 * 1024.0);
    return QStringLiteral("%1 MB").arg(mb, 0, 'f', 3);
}

QString MediaInfoWidget::formatDuration(double seconds)
{
    if (seconds <= 0) return QStringLiteral("N/A");
    int totalMs = static_cast<int>(seconds * 1000.0);
    int h = totalMs / 3600000;
    int m = (totalMs % 3600000) / 60000;
    int s = (totalMs % 60000) / 1000;
    int ms = totalMs % 1000;
    return QStringLiteral("%1:%2:%3 (total %4 s)")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(qRound(seconds));
}

QString MediaInfoWidget::formatBitrate(int64_t bps)
{
    if (bps <= 0) return QStringLiteral("0 Kbps");
    if (bps >= 1000000) {
        double mbps = bps / 1000000.0;
        return QStringLiteral("%1 Mbps").arg(mbps, 0, 'f', 3);
    }
    double kbps = bps / 1000.0;
    return QStringLiteral("%1 Kbps").arg(qRound(kbps));
}

double MediaInfoWidget::computeAverageGopSize(const QVector<PacketInfo> &packets, int streamIndex)
{
    // 收集该流的关键帧位置
    QVector<int> keyFramePositions;
    int frameCount = 0;
    for (const auto &p : packets) {
        if (p.streamIndex != streamIndex) continue;
        if (p.mediaType != AVMEDIA_TYPE_VIDEO) continue;
        if (p.flags & AV_PKT_FLAG_KEY)
            keyFramePositions.append(frameCount);
        frameCount++;
    }

    if (keyFramePositions.size() < 2) return 0.0;

    double totalGopSize = 0.0;
    for (int i = 1; i < keyFramePositions.size(); ++i) {
        totalGopSize += (keyFramePositions[i] - keyFramePositions[i - 1]);
    }
    return totalGopSize / (keyFramePositions.size() - 1);
}

QString MediaInfoWidget::formatAspectRatio(int num, int den)
{
    if (num <= 0 || den <= 0) return QStringLiteral("[0:0]");
    // 求最大公约数化简
    int a = num, b = den;
    while (b) { int t = b; b = a % b; a = t; }
    return QStringLiteral("[%1:%2]").arg(num / a).arg(den / a);
}

// ===== 私有辅助 =====

QTreeWidgetItem *MediaInfoWidget::addSection(const QString &title, const QColor &bgColor)
{
    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, title);
    item->setBackground(0, QBrush(bgColor));
    item->setBackground(1, QBrush(bgColor));
    item->setForeground(0, QBrush(Qt::white));
    item->setForeground(1, QBrush(Qt::white));
    QFont boldFont = item->font(0);
    boldFont.setBold(true);
    item->setFont(0, boldFont);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    return item;
}

void MediaInfoWidget::addField(QTreeWidgetItem *section, const QString &key, const QString &value)
{
    auto *item = new QTreeWidgetItem(section);
    item->setText(0, QStringLiteral("    %1").arg(key));
    item->setText(1, QStringLiteral(": %1").arg(value));
}
