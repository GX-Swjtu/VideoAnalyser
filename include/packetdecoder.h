#ifndef PACKETDECODER_H
#define PACKETDECODER_H

#include <QImage>
#include <QString>
#include "packetreader.h"
#include "audiowaveformwidget.h"

struct AVFormatContext;
struct SwsContext;

// 解码器实例 — 持有独立的 AVFormatContext 以复用，避免每次解码重新 open + find_stream_info。
// 每个实例绑定一个 PacketReader（即一个文件），可在后台线程中使用。
// 注意：PacketReader 指针仅用于读取元数据（packetAt / streams / findGopKeyFrame），
//       不会调用 readPacketData()，因此不会与主线程的 PacketReader 状态冲突。
class PacketDecoder {
public:
    explicit PacketDecoder(PacketReader *reader);
    ~PacketDecoder();

    // 禁止拷贝
    PacketDecoder(const PacketDecoder &) = delete;
    PacketDecoder &operator=(const PacketDecoder &) = delete;

    // 打开独立的 AVFormatContext（调用 avformat_open_input + avformat_find_stream_info）
    bool open(QString *errorMsg = nullptr);
    void close();
    bool isOpen() const { return m_fmtCtx != nullptr; }

    // 视频解码（追帧）- 返回解码后的 QImage，失败返回空 QImage
    QImage decodeVideoPacket(int packetIndex, QString *errorMsg = nullptr);

    // 音频解码 - 返回 PCM 浮点数据
    AudioData decodeAudioPacket(int packetIndex, QString *errorMsg = nullptr);

    // 字幕解码 - 返回文本（不使用 m_fmtCtx，通过 readPacketData 读取原始数据）
    QString decodeSubtitlePacket(int packetIndex, QString *errorMsg = nullptr);

    static QString ffmpegError(int errnum);

private:
    PacketReader *m_reader = nullptr;
    AVFormatContext *m_fmtCtx = nullptr;

    // SwsContext 缓存（视频色彩转换复用）
    SwsContext *m_swsCtx = nullptr;
    int m_swsWidth = 0;
    int m_swsHeight = 0;
    int m_swsSrcFmt = -1; // AVPixelFormat
};

#endif // PACKETDECODER_H
