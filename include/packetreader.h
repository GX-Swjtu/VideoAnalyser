#ifndef PACKETREADER_H
#define PACKETREADER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QByteArray>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

// 单个 Packet 的元数据（不含原始数据）
struct PacketInfo {
    int index;                // 全局序号（从 0 开始）
    int streamIndex;          // 所属流索引
    AVMediaType mediaType;    // AVMEDIA_TYPE_VIDEO / AUDIO / SUBTITLE / DATA
    QString codecName;        // 编解码器名称，如 "h264", "aac"
    int64_t pts;              // 显示时间戳（原始值，time_base 单位）
    int64_t dts;              // 解码时间戳（原始值）
    double ptsTime;           // PTS 转换为秒
    double dtsTime;           // DTS 转换为秒
    int size;                 // 数据大小（字节）
    int64_t pos;              // 文件字节偏移（-1 为未知）
    int flags;                // 标志位：AV_PKT_FLAG_KEY, AV_PKT_FLAG_CORRUPT 等
    int64_t duration;         // 持续时间（time_base 单位）
    double durationTime;      // 持续时间（秒）
    int gopKeyFrameIndex;     // 所属 GOP 的关键帧在全局列表中的序号（视频流用）
};

// 流信息
struct StreamInfo {
    int index;                // 流索引
    AVMediaType mediaType;    // 媒体类型
    QString codecName;        // 编解码器名称
    AVCodecParameters *codecpar = nullptr; // 编解码参数（深拷贝，需手动释放）
    AVRational timeBase;      // 时间基
    AVRational avgFrameRate;  // 平均帧率（视频流）
    int64_t duration;         // 流时长（time_base 单位）
    int64_t nbFrames;         // 帧数
    int sampleRate;           // 采样率（音频流）
    int channels;             // 声道数（音频流）
    QString title;            // 流标题（来自 metadata）
    QString language;         // 语言（来自 metadata）
};

class PacketReader : public QObject {
    Q_OBJECT
public:
    explicit PacketReader(QObject *parent = nullptr);
    ~PacketReader();

    bool open(const QString &filePath);
    bool readAllPackets();   // 遍历所有 Packet，填充 m_packets
    void close();

    // 访问器
    int packetCount() const;
    const PacketInfo &packetAt(int index) const;
    const QVector<PacketInfo> &packets() const;
    const QVector<StreamInfo> &streams() const;
    QString formatName() const;      // 封装格式名称
    double durationSeconds() const;  // 总时长（秒）
    int64_t bitRate() const;         // 总比特率

    // 按需读取原始数据（用于 Hex 视图）
    QByteArray readPacketData(int packetIndex);

    // 获取 AVFormatContext（解码器需要用）
    AVFormatContext *formatContext() const;

    // 关键帧索引查询
    int findGopKeyFrame(int packetIndex) const;

    bool isOpen() const { return m_formatCtx != nullptr; }

signals:
    void progressChanged(int current, int total);  // 读取进度

private:
    AVFormatContext *m_formatCtx = nullptr;
    QVector<PacketInfo> m_packets;
    QVector<StreamInfo> m_streams;
    QMap<int, QVector<int>> m_keyFrameIndices;  // streamIndex → 关键帧全局序号列表
    QString m_filePath;
};

#endif // PACKETREADER_H
