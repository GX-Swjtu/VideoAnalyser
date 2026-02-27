#include "packetdetailwidget.h"
#include "packetreader.h"
#include "packetlistmodel.h"
#include "packetdecoder.h"
#include "hexviewwidget.h"
#include "audiowaveformwidget.h"
#include "audiospectrogramwidget.h"

#include <QVBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTabWidget>
#include <QLabel>
#include <QTextEdit>
#include <QScrollArea>
#include <QApplication>
#include <QResizeEvent>

// --- ScalableImageLabel implementation ---
ScalableImageLabel::ScalableImageLabel(QWidget *parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(1, 1);
}

void ScalableImageLabel::setOriginalImage(const QImage &image)
{
    m_originalImage = image;
    updateScaledPixmap();
}

void ScalableImageLabel::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    if (!m_originalImage.isNull()) {
        updateScaledPixmap();
    }
}

void ScalableImageLabel::updateScaledPixmap()
{
    if (m_originalImage.isNull() || width() <= 0 || height() <= 0)
        return;
    setPixmap(QPixmap::fromImage(m_originalImage).scaled(
        size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

extern "C" {
#include <libavutil/avutil.h>
}

PacketDetailWidget::PacketDetailWidget(PacketReader *reader, int packetIndex, QWidget *parent)
    : QWidget(parent)
    , m_packetIndex(packetIndex)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_splitter = new QSplitter(Qt::Horizontal, this);

    // 左侧：内容标签页（Hex、视频帧、波形等）
    m_contentTabs = new QTabWidget();
    m_splitter->addWidget(m_contentTabs);

    // 右侧：元数据树
    m_metadataTree = new QTreeWidget();
    m_metadataTree->setHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
    m_metadataTree->setColumnCount(2);
    m_metadataTree->setAlternatingRowColors(true);
    m_splitter->addWidget(m_metadataTree);

    m_metadataTree->setMinimumWidth(280);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 1);
    // 设置初始分割比例：左侧 600，右侧 300
    m_splitter->setSizes({600, 300});

    layout->addWidget(m_splitter);

    buildMetadataTree(reader, packetIndex);
    buildContentTabs(reader, packetIndex);

    m_metadataTree->expandAll();
    m_metadataTree->resizeColumnToContents(0);
    m_metadataTree->resizeColumnToContents(1);
}

void PacketDetailWidget::buildMetadataTree(PacketReader *reader, int packetIndex)
{
    const PacketInfo &pkt = reader->packetAt(packetIndex);
    const QVector<StreamInfo> &streams = reader->streams();

    // 基本信息
    auto *basicGroup = new QTreeWidgetItem(m_metadataTree, {QStringLiteral("基本信息")});
    new QTreeWidgetItem(basicGroup, {QStringLiteral("Packet 序号"), QString::number(pkt.index)});
    new QTreeWidgetItem(basicGroup, {QStringLiteral("流索引"), QString::number(pkt.streamIndex)});
    new QTreeWidgetItem(basicGroup, {QStringLiteral("媒体类型"), PacketListModel::mediaTypeString(pkt.mediaType)});
    new QTreeWidgetItem(basicGroup, {QStringLiteral("编解码器"), pkt.codecName});

    // 时间信息
    auto *timeGroup = new QTreeWidgetItem(m_metadataTree, {QStringLiteral("时间信息")});
    {
        QString ptsStr = (pkt.pts != AV_NOPTS_VALUE)
                             ? QStringLiteral("%1 (%2)").arg(pkt.pts).arg(PacketListModel::formatTime(pkt.ptsTime))
                             : QStringLiteral("N/A");
        new QTreeWidgetItem(timeGroup, {QStringLiteral("PTS"), ptsStr});

        QString dtsStr = (pkt.dts != AV_NOPTS_VALUE)
                             ? QStringLiteral("%1 (%2)").arg(pkt.dts).arg(PacketListModel::formatTime(pkt.dtsTime))
                             : QStringLiteral("N/A");
        new QTreeWidgetItem(timeGroup, {QStringLiteral("DTS"), dtsStr});

        new QTreeWidgetItem(timeGroup, {QStringLiteral("Duration"),
                                        QStringLiteral("%1 (%2)").arg(pkt.duration).arg(PacketListModel::formatTime(pkt.durationTime))});

        if (pkt.streamIndex >= 0 && pkt.streamIndex < streams.size()) {
            const StreamInfo &si = streams[pkt.streamIndex];
            new QTreeWidgetItem(timeGroup, {QStringLiteral("Time Base"),
                                            QStringLiteral("%1/%2").arg(si.timeBase.num).arg(si.timeBase.den)});
        }
    }

    // 文件信息
    auto *fileGroup = new QTreeWidgetItem(m_metadataTree, {QStringLiteral("文件信息")});
    new QTreeWidgetItem(fileGroup, {QStringLiteral("偏移地址"), PacketListModel::formatOffset(pkt.pos)});
    new QTreeWidgetItem(fileGroup, {QStringLiteral("数据大小"), QStringLiteral("%1 bytes").arg(pkt.size)});

    // 标志
    auto *flagsGroup = new QTreeWidgetItem(m_metadataTree, {QStringLiteral("标志")});
    new QTreeWidgetItem(flagsGroup, {QStringLiteral("关键帧"),
                                     (pkt.flags & AV_PKT_FLAG_KEY) ? QStringLiteral("是") : QStringLiteral("否")});
    new QTreeWidgetItem(flagsGroup, {QStringLiteral("损坏"),
                                     (pkt.flags & AV_PKT_FLAG_CORRUPT) ? QStringLiteral("是") : QStringLiteral("否")});
    new QTreeWidgetItem(flagsGroup, {QStringLiteral("可丢弃"),
                                     (pkt.flags & AV_PKT_FLAG_DISCARD) ? QStringLiteral("是") : QStringLiteral("否")});

    // GOP 信息（仅视频流）
    if (pkt.mediaType == AVMEDIA_TYPE_VIDEO) {
        auto *gopGroup = new QTreeWidgetItem(m_metadataTree, {QStringLiteral("GOP 信息")});
        if (pkt.gopKeyFrameIndex >= 0) {
            int distance = pkt.index - pkt.gopKeyFrameIndex;
            new QTreeWidgetItem(gopGroup, {QStringLiteral("关键帧序号"),
                                           QStringLiteral("#%1 (距离: %2 packets)").arg(pkt.gopKeyFrameIndex).arg(distance)});
        } else {
            new QTreeWidgetItem(gopGroup, {QStringLiteral("关键帧序号"), QStringLiteral("N/A")});
        }
    }
}

void PacketDetailWidget::buildContentTabs(PacketReader *reader, int packetIndex)
{
    const PacketInfo &pkt = reader->packetAt(packetIndex);

    // 优先添加解码内容标签，Hex 放在最后

    // 视频帧（优先显示）
    if (pkt.mediaType == AVMEDIA_TYPE_VIDEO) {
        auto *imageLabel = new ScalableImageLabel();

        // 尝试解码
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString errMsg;
        QImage frame = PacketDecoder::decodeVideoPacket(reader, packetIndex, &errMsg);
        QApplication::restoreOverrideCursor();

        if (!frame.isNull()) {
            imageLabel->setOriginalImage(frame);
        } else {
            imageLabel->setText(QStringLiteral("解码失败: %1").arg(errMsg));
        }

        m_contentTabs->addTab(imageLabel, QStringLiteral("视频帧"));
    }

    // 音频频谱 + 波形（优先显示）
    if (pkt.mediaType == AVMEDIA_TYPE_AUDIO) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString errMsg;
        AudioData audioData = PacketDecoder::decodeAudioPacket(reader, packetIndex, &errMsg);
        QApplication::restoreOverrideCursor();

        // 频谱图（第一标签页）
        auto *spectrogram = new AudioSpectrogramWidget();
        if (!audioData.samples.isEmpty()) {
            spectrogram->setAudioData(audioData);
        }
        m_contentTabs->addTab(spectrogram, QStringLiteral("频谱"));

        // 波形图（第二标签页）
        auto *waveform = new AudioWaveformWidget();
        if (!audioData.samples.isEmpty()) {
            waveform->setAudioData(audioData);
        }
        m_contentTabs->addTab(waveform, QStringLiteral("波形"));
    }

    // 字幕文本（优先显示）
    if (pkt.mediaType == AVMEDIA_TYPE_SUBTITLE) {
        auto *textEdit = new QTextEdit();
        textEdit->setReadOnly(true);

        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString errMsg;
        QString text = PacketDecoder::decodeSubtitlePacket(reader, packetIndex, &errMsg);
        QApplication::restoreOverrideCursor();

        if (!text.isEmpty()) {
            textEdit->setText(text);
        } else {
            textEdit->setText(QStringLiteral("无字幕内容: %1").arg(errMsg));
        }
        m_contentTabs->addTab(textEdit, QStringLiteral("字幕"));
    }

    // Hex 视图放在最后（所有类型都有）
    QByteArray rawData = reader->readPacketData(packetIndex);
    auto *hexView = new HexViewWidget();
    hexView->setData(rawData);
    hexView->setBaseOffset(pkt.pos >= 0 ? pkt.pos : 0);
    m_contentTabs->addTab(hexView, QStringLiteral("Hex"));

    // 默认选中第一个标签（解码内容）
    m_contentTabs->setCurrentIndex(0);
}
