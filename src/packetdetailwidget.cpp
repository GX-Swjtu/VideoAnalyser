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
#include <QtConcurrent/QtConcurrent>

#include <memory>

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
    , m_reader(reader)
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

    // Hex 懒加载：切换 tab 时按需读取
    connect(m_contentTabs, &QTabWidget::currentChanged, this, &PacketDetailWidget::onTabChanged);

    m_metadataTree->expandAll();
    m_metadataTree->resizeColumnToContents(0);
    m_metadataTree->resizeColumnToContents(1);
}

PacketDetailWidget::~PacketDetailWidget()
{
    // QFutureWatcher 析构时会 waitForFinished()，保证后台线程安全结束
    delete m_videoWatcher;
    delete m_audioWatcher;
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

    // 视频帧（异步解码）
    if (pkt.mediaType == AVMEDIA_TYPE_VIDEO) {
        m_imageLabel = new ScalableImageLabel();
        m_imageLabel->setText(QStringLiteral("正在解码..."));
        m_contentTabs->addTab(m_imageLabel, QStringLiteral("视频帧"));

        // 在后台线程创建独立的 PacketDecoder 实例并执行解码
        // 使用 shared_ptr 保证 lambda 中安全持有生命周期
        auto decoder = std::make_shared<PacketDecoder>(reader);
        int idx = packetIndex;

        m_videoWatcher = new QFutureWatcher<VideoDecodeResult>(this);
        connect(m_videoWatcher, &QFutureWatcher<VideoDecodeResult>::finished,
                this, &PacketDetailWidget::onVideoDecoded);

        QFuture<VideoDecodeResult> future = QtConcurrent::run([decoder, idx]() -> VideoDecodeResult {
            VideoDecodeResult res;
            if (!decoder->open(&res.error)) {
                qWarning() << "PacketDecoder open failed:" << res.error;
                return res;
            }
            res.image = decoder->decodeVideoPacket(idx, &res.error);
            return res;
        });
        m_videoWatcher->setFuture(future);
    }

    // 音频频谱 + 波形（异步解码）
    if (pkt.mediaType == AVMEDIA_TYPE_AUDIO) {
        m_spectrogram = new AudioSpectrogramWidget();
        m_contentTabs->addTab(m_spectrogram, QStringLiteral("频谱"));

        m_waveform = new AudioWaveformWidget();
        m_contentTabs->addTab(m_waveform, QStringLiteral("波形"));

        auto decoder = std::make_shared<PacketDecoder>(reader);
        int idx = packetIndex;

        m_audioWatcher = new QFutureWatcher<AudioData>(this);
        connect(m_audioWatcher, &QFutureWatcher<AudioData>::finished,
                this, &PacketDetailWidget::onAudioDecoded);

        QFuture<AudioData> future = QtConcurrent::run([decoder, idx]() -> AudioData {
            QString err;
            if (!decoder->open(&err)) {
                qWarning() << "PacketDecoder open failed:" << err;
                return AudioData();
            }
            return decoder->decodeAudioPacket(idx, nullptr);
        });
        m_audioWatcher->setFuture(future);
    }

    // 字幕文本（同步解码，通常很快）
    if (pkt.mediaType == AVMEDIA_TYPE_SUBTITLE) {
        auto *textEdit = new QTextEdit();
        textEdit->setReadOnly(true);

        PacketDecoder decoder(reader);
        QString errMsg;
        QString text;
        // 字幕解码通过 readPacketData 读原始数据，不需要 open m_fmtCtx
        // 但为了保持接口一致性仍使用实例方法
        text = decoder.decodeSubtitlePacket(packetIndex, &errMsg);

        if (!text.isEmpty()) {
            textEdit->setText(text);
        } else {
            textEdit->setText(QStringLiteral("无字幕内容: %1").arg(errMsg));
        }
        m_contentTabs->addTab(textEdit, QStringLiteral("字幕"));
    }

    // Hex 视图放在最后（所有类型都有）— 懒加载，不立即读取数据
    m_hexView = new HexViewWidget();
    m_hexView->setBaseOffset(pkt.pos >= 0 ? pkt.pos : 0);
    m_hexTabIndex = m_contentTabs->addTab(m_hexView, QStringLiteral("Hex"));

    // 默认选中第一个标签（解码内容）
    m_contentTabs->setCurrentIndex(0);
}

// ---- 异步解码回调 ----

void PacketDetailWidget::onVideoDecoded()
{
    if (!m_videoWatcher || !m_imageLabel) return;

    VideoDecodeResult res = m_videoWatcher->result();
    if (!res.image.isNull()) {
        m_imageLabel->setOriginalImage(res.image);
    } else {
        QString msg = QStringLiteral("解码失败");
        if (!res.error.isEmpty())
            msg += QStringLiteral(": %1").arg(res.error);
        m_imageLabel->setText(msg);
    }
}

void PacketDetailWidget::onAudioDecoded()
{
    if (!m_audioWatcher) return;

    AudioData audioData = m_audioWatcher->result();
    if (!audioData.samples.isEmpty()) {
        if (m_spectrogram) m_spectrogram->setAudioData(audioData);
        if (m_waveform) m_waveform->setAudioData(audioData);
    }
}

// ---- Hex 懒加载 ----

void PacketDetailWidget::onTabChanged(int index)
{
    if (index == m_hexTabIndex && !m_hexLoaded && m_hexView && m_reader) {
        QByteArray rawData = m_reader->readPacketData(m_packetIndex);
        m_hexView->setData(rawData);
        m_hexLoaded = true;
    }
}
