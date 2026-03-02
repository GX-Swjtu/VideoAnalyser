#ifndef PACKETDETAILWIDGET_H
#define PACKETDETAILWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QImage>
#include <QFutureWatcher>

#include "audiowaveformwidget.h" // AudioData

// 视频解码结果（异步传回 图像 + 错误信息）
struct VideoDecodeResult {
    QImage image;
    QString error;
};

class PacketReader;
class PacketDecoder;
class QTreeWidget;
class QTabWidget;
class QSplitter;
class HexViewWidget;
class AudioSpectrogramWidget;

// 自适应大小的图片标签
class ScalableImageLabel : public QLabel {
    Q_OBJECT
public:
    explicit ScalableImageLabel(QWidget *parent = nullptr);
    void setOriginalImage(const QImage &image);
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    void updateScaledPixmap();
    QImage m_originalImage;
};

class PacketDetailWidget : public QWidget {
    Q_OBJECT
public:
    explicit PacketDetailWidget(PacketReader *reader, int packetIndex, QWidget *parent = nullptr);
    ~PacketDetailWidget();

    int packetIndex() const { return m_packetIndex; }

private slots:
    void onVideoDecoded();
    void onAudioDecoded();
    void onTabChanged(int index);

private:
    void buildMetadataTree(PacketReader *reader, int packetIndex);
    void buildContentTabs(PacketReader *reader, int packetIndex);

    int m_packetIndex;
    PacketReader *m_reader = nullptr;
    QTreeWidget *m_metadataTree = nullptr;
    QTabWidget *m_contentTabs = nullptr;
    QSplitter *m_splitter = nullptr;

    // 异步解码
    QFutureWatcher<VideoDecodeResult> *m_videoWatcher = nullptr;
    QFutureWatcher<AudioData> *m_audioWatcher = nullptr;

    // 解码结果接收控件（由异步回调更新）
    ScalableImageLabel *m_imageLabel = nullptr;
    AudioWaveformWidget *m_waveform = nullptr;
    AudioSpectrogramWidget *m_spectrogram = nullptr;

    // Hex 懒加载
    HexViewWidget *m_hexView = nullptr;
    int m_hexTabIndex = -1;
    bool m_hexLoaded = false;
};

#endif // PACKETDETAILWIDGET_H
