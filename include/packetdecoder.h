#ifndef PACKETDECODER_H
#define PACKETDECODER_H

#include <QImage>
#include <QString>
#include "packetreader.h"
#include "audiowaveformwidget.h"

class PacketDecoder {
public:
    // 视频解码（追帧）- 返回解码后的 QImage，失败返回空 QImage
    static QImage decodeVideoPacket(PacketReader *reader, int packetIndex, QString *errorMsg = nullptr);

    // 音频解码 - 返回 PCM 浮点数据
    static AudioData decodeAudioPacket(PacketReader *reader, int packetIndex, QString *errorMsg = nullptr);

    // 字幕解码 - 返回文本
    static QString decodeSubtitlePacket(PacketReader *reader, int packetIndex, QString *errorMsg = nullptr);

private:
    static QString ffmpegError(int errnum);
};

#endif // PACKETDECODER_H
