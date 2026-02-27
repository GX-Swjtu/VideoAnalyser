#include "packetdecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <QDebug>

// ---- 构造 / 析构 / open / close ----

PacketDecoder::PacketDecoder(PacketReader *reader)
    : m_reader(reader)
{
}

PacketDecoder::~PacketDecoder()
{
    close();
}

bool PacketDecoder::open(QString *errorMsg)
{
    if (m_fmtCtx) return true; // 已经打开
    if (!m_reader || !m_reader->isOpen()) {
        if (errorMsg) *errorMsg = QStringLiteral("Reader is null or not open");
        return false;
    }

    QByteArray pathUtf8 = m_reader->filePath().toUtf8();
    int ret = avformat_open_input(&m_fmtCtx, pathUtf8.constData(), nullptr, nullptr);
    if (ret < 0) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to open file: %1").arg(ffmpegError(ret));
        m_fmtCtx = nullptr;
        return false;
    }
    ret = avformat_find_stream_info(m_fmtCtx, nullptr);
    if (ret < 0) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to find stream info: %1").arg(ffmpegError(ret));
        avformat_close_input(&m_fmtCtx);
        return false;
    }
    return true;
}

void PacketDecoder::close()
{
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
        m_swsWidth = 0;
        m_swsHeight = 0;
        m_swsSrcFmt = -1;
    }
    if (m_fmtCtx) {
        avformat_close_input(&m_fmtCtx);
    }
}

// ---- 静态工具函数 ----

QString PacketDecoder::ffmpegError(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

QImage PacketDecoder::decodeVideoPacket(int packetIndex, QString *errorMsg)
{
    if (!m_reader || packetIndex < 0 || packetIndex >= m_reader->packetCount()) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid packet index");
        return QImage();
    }

    const PacketInfo &targetPkt = m_reader->packetAt(packetIndex);
    if (targetPkt.mediaType != AVMEDIA_TYPE_VIDEO) {
        if (errorMsg) *errorMsg = QStringLiteral("Not a video packet");
        return QImage();
    }

    if (targetPkt.streamIndex < 0 || targetPkt.streamIndex >= m_reader->streams().size()) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid stream index");
        return QImage();
    }

    if (!m_fmtCtx) {
        if (errorMsg) *errorMsg = QStringLiteral("Decoder not opened, call open() first");
        return QImage();
    }

    const StreamInfo &streamInfo = m_reader->streams()[targetPkt.streamIndex];

    // 找到解码器
    const AVCodec *codec = avcodec_find_decoder(streamInfo.codecpar->codec_id);
    if (!codec) {
        if (errorMsg) *errorMsg = QStringLiteral("Decoder not found for codec: %1").arg(streamInfo.codecName);
        return QImage();
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to allocate codec context");
        return QImage();
    }

    int ret = avcodec_parameters_to_context(codecCtx, streamInfo.codecpar);
    if (ret < 0) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to copy codec params: %1").arg(ffmpegError(ret));
        avcodec_free_context(&codecCtx);
        return QImage();
    }

    codecCtx->pkt_timebase = streamInfo.timeBase;

    ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to open decoder: %1").arg(ffmpegError(ret));
        avcodec_free_context(&codecCtx);
        return QImage();
    }

    // 找到 GOP 起始关键帧
    int gopStart = m_reader->findGopKeyFrame(packetIndex);
    if (gopStart < 0) gopStart = 0; // 找不到就从头开始

    const PacketInfo &gopPkt = m_reader->packetAt(gopStart);

    // Seek 到关键帧位置（复用 m_fmtCtx）
    int64_t seekTs = (gopPkt.dts != AV_NOPTS_VALUE) ? gopPkt.dts : gopPkt.pts;
    if (seekTs == AV_NOPTS_VALUE) seekTs = 0;

    ret = av_seek_frame(m_fmtCtx, targetPkt.streamIndex, seekTs, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        // 尝试 seek 到文件开头
        av_seek_frame(m_fmtCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
    }

    avcodec_flush_buffers(codecCtx);

    // 连续读取直到找到目标帧
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    QImage result;

    bool found = false;
    int maxPackets = 5000; // 防止无限循环

    // 辅助 lambda：从解码器接收帧并匹配目标 PTS
    auto drainFrames = [&]() {
        while (!found) {
            ret = avcodec_receive_frame(codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            // PTS 匹配
            if (frame->pts == targetPkt.pts) {
                int w = frame->width;
                int h = frame->height;
                AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);

                // 复用或重建 SwsContext
                if (m_swsCtx && (m_swsWidth != w || m_swsHeight != h || m_swsSrcFmt != srcFmt)) {
                    sws_freeContext(m_swsCtx);
                    m_swsCtx = nullptr;
                }
                if (!m_swsCtx) {
                    m_swsCtx = sws_getContext(
                        w, h, srcFmt,
                        w, h, AV_PIX_FMT_RGB24,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
                    m_swsWidth = w;
                    m_swsHeight = h;
                    m_swsSrcFmt = srcFmt;
                }

                if (m_swsCtx) {
                    AVFrame *rgbFrame = av_frame_alloc();
                    rgbFrame->format = AV_PIX_FMT_RGB24;
                    rgbFrame->width = w;
                    rgbFrame->height = h;
                    av_frame_get_buffer(rgbFrame, 0);

                    sws_scale(m_swsCtx, frame->data, frame->linesize, 0, h,
                              rgbFrame->data, rgbFrame->linesize);

                    QImage img(rgbFrame->data[0], rgbFrame->width, rgbFrame->height,
                               rgbFrame->linesize[0], QImage::Format_RGB888);
                    result = img.copy(); // 深拷贝

                    av_frame_free(&rgbFrame);
                }

                found = true;
                av_frame_unref(frame);
                break;
            }

            av_frame_unref(frame);
        }
    };

    while (!found && maxPackets-- > 0) {
        ret = av_read_frame(m_fmtCtx, pkt);
        if (ret < 0) break; // EOF 或错误，后续 drain 处理

        if (pkt->stream_index != targetPkt.streamIndex) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(codecCtx, pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            av_packet_unref(pkt);
            continue;
        }

        drainFrames();

        // 超过目标 DTS 则停止（使用帧 duration 的 50 倍作为余量）
        if (!found && targetPkt.dts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE) {
            int64_t margin = (targetPkt.duration > 0) ? targetPkt.duration * 50 : 100000;
            if (pkt->dts > targetPkt.dts + margin) {
                av_packet_unref(pkt);
                break;
            }
        }

        av_packet_unref(pkt);
    }

    // EOF 后 drain 解码器：发送 NULL packet 刷出缓冲区中剩余的帧
    if (!found) {
        avcodec_send_packet(codecCtx, nullptr);
        drainFrames();
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);

    if (!found && errorMsg) {
        *errorMsg = QStringLiteral("Failed to decode target frame (PTS=%1)").arg(targetPkt.pts);
    }

    return result;
}

AudioData PacketDecoder::decodeAudioPacket(int packetIndex, QString *errorMsg)
{
    AudioData audioData;

    if (!m_reader || packetIndex < 0 || packetIndex >= m_reader->packetCount()) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid packet index");
        return audioData;
    }

    const PacketInfo &targetPkt = m_reader->packetAt(packetIndex);
    if (targetPkt.mediaType != AVMEDIA_TYPE_AUDIO) {
        if (errorMsg) *errorMsg = QStringLiteral("Not an audio packet");
        return audioData;
    }

    if (targetPkt.streamIndex < 0 || targetPkt.streamIndex >= m_reader->streams().size()) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid stream index");
        return audioData;
    }

    if (!m_fmtCtx) {
        if (errorMsg) *errorMsg = QStringLiteral("Decoder not opened, call open() first");
        return audioData;
    }

    const StreamInfo &streamInfo = m_reader->streams()[targetPkt.streamIndex];

    const AVCodec *codec = avcodec_find_decoder(streamInfo.codecpar->codec_id);
    if (!codec) {
        if (errorMsg) *errorMsg = QStringLiteral("Decoder not found");
        return audioData;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to allocate codec context");
        return audioData;
    }

    int ret = avcodec_parameters_to_context(codecCtx, streamInfo.codecpar);
    if (ret < 0) {
        avcodec_free_context(&codecCtx);
        if (errorMsg) *errorMsg = ffmpegError(ret);
        return audioData;
    }

    codecCtx->pkt_timebase = streamInfo.timeBase;

    ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        avcodec_free_context(&codecCtx);
        if (errorMsg) *errorMsg = ffmpegError(ret);
        return audioData;
    }

    // Seek 到目标 Packet 前方（复用 m_fmtCtx），预留若干帧给解码器预热
    int64_t seekTs = (targetPkt.dts != AV_NOPTS_VALUE) ? targetPkt.dts : targetPkt.pts;
    if (seekTs == AV_NOPTS_VALUE) seekTs = 0;
    int64_t warmupOffset = (targetPkt.duration > 0) ? targetPkt.duration * 3 : 10000;
    seekTs -= warmupOffset;

    av_seek_frame(m_fmtCtx, targetPkt.streamIndex, seekTs, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecCtx);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool found = false;
    bool sentTarget = false;
    int maxPackets = 5000;

    // 帧数据提取 lambda：将解码帧转为 AudioData（交错 float）
    auto extractFrameAudio = [&](AVFrame *frm) -> bool {
        int nbSamples = frm->nb_samples;
        int chCount = frm->ch_layout.nb_channels;
        audioData.sampleRate = frm->sample_rate;
        audioData.channels = chCount;

        AVSampleFormat fmt = static_cast<AVSampleFormat>(frm->format);

        if (fmt == AV_SAMPLE_FMT_FLTP) {
            // 最常见情况：AAC 输出 planar float，直接交错拷贝
            audioData.samples.resize(nbSamples * chCount);
            for (int ch = 0; ch < chCount; ++ch) {
                const float *src = reinterpret_cast<const float*>(frm->extended_data[ch]);
                for (int i = 0; i < nbSamples; ++i) {
                    audioData.samples[i * chCount + ch] = src[i];
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_FLT && !av_sample_fmt_is_planar(fmt)) {
            // 已经是 float interleaved
            audioData.samples.resize(nbSamples * chCount);
            memcpy(audioData.samples.data(), frm->data[0], nbSamples * chCount * sizeof(float));
        } else {
            // 其他格式：使用 libswresample 转换为 float 交错格式
            SwrContext *swr = nullptr;
            AVChannelLayout outLayout;
            av_channel_layout_copy(&outLayout, &frm->ch_layout);

            int r = swr_alloc_set_opts2(&swr,
                                        &outLayout, AV_SAMPLE_FMT_FLT, frm->sample_rate,
                                        &frm->ch_layout, fmt, frm->sample_rate,
                                        0, nullptr);
            av_channel_layout_uninit(&outLayout);

            if (r >= 0 && swr) {
                r = swr_init(swr);
                if (r >= 0) {
                    audioData.samples.resize(nbSamples * chCount);
                    uint8_t *outBuf = reinterpret_cast<uint8_t*>(audioData.samples.data());
                    int converted = swr_convert(swr, &outBuf, nbSamples,
                                const_cast<const uint8_t**>(frm->extended_data), nbSamples);
                    if (converted <= 0) {
                        qWarning() << "swr_convert failed or produced 0 samples:" << converted;
                        audioData.samples.clear();
                    }
                } else {
                    qWarning() << "swr_init failed:" << ffmpegError(r);
                }
                swr_free(&swr);
            } else {
                qWarning() << "swr_alloc_set_opts2 failed";
            }
        }
        return !audioData.samples.isEmpty();
    };

    while (!found && maxPackets-- > 0) {
        ret = av_read_frame(m_fmtCtx, pkt);
        if (ret < 0) break;

        if (pkt->stream_index != targetPkt.streamIndex) {
            av_packet_unref(pkt);
            continue;
        }

        // 追踪是否已发送目标 Packet（按 pos 或 pts 匹配）
        if (!sentTarget) {
            if (targetPkt.pos >= 0) {
                sentTarget = (pkt->pos == targetPkt.pos && pkt->size == targetPkt.size);
            } else {
                sentTarget = (pkt->pts == targetPkt.pts);
            }
        }

        ret = avcodec_send_packet(codecCtx, pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            av_packet_unref(pkt);
            continue;
        }

        while (true) {
            ret = avcodec_receive_frame(codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            // 帧匹配策略：
            // 1. PTS 精确匹配（主要方式，与视频解码一致）
            // 2. 回退：如果目标包已发送且 PTS 匹配失败（AAC priming 帧 PTS 被解码器调整），
            //    接受目标包发送后的第一个解码帧
            bool ptsMatch = (frame->pts == targetPkt.pts);
            bool fallbackMatch = (sentTarget && !ptsMatch && frame->pts >= 0);

            if (ptsMatch || fallbackMatch) {
                found = extractFrameAudio(frame);
                av_frame_unref(frame);
                break;
            }

            av_frame_unref(frame);
        }

        // 超过目标 DTS 则停止
        if (!found && targetPkt.dts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE) {
            int64_t margin = (targetPkt.duration > 0) ? targetPkt.duration * 50 : 100000;
            if (pkt->dts > targetPkt.dts + margin) {
                av_packet_unref(pkt);
                break;
            }
        }

        av_packet_unref(pkt);
    }

    // Drain 解码器：发送 NULL packet 让解码器吐出剩余缓冲帧
    if (!found) {
        avcodec_send_packet(codecCtx, nullptr);
        while (true) {
            ret = avcodec_receive_frame(codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            bool ptsMatch = (frame->pts == targetPkt.pts);
            bool fallbackMatch = (sentTarget && !ptsMatch && frame->pts >= 0);
            if (ptsMatch || fallbackMatch) {
                found = extractFrameAudio(frame);
            }
            av_frame_unref(frame);
            if (found) break;
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);

    if (!found && errorMsg) {
        *errorMsg = QStringLiteral("Failed to decode target audio packet (PTS=%1)").arg(targetPkt.pts);
    }

    return audioData;
}

QString PacketDecoder::decodeSubtitlePacket(int packetIndex, QString *errorMsg)
{
    if (!m_reader || packetIndex < 0 || packetIndex >= m_reader->packetCount()) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid packet index");
        return QString();
    }

    const PacketInfo &targetPkt = m_reader->packetAt(packetIndex);
    if (targetPkt.mediaType != AVMEDIA_TYPE_SUBTITLE) {
        if (errorMsg) *errorMsg = QStringLiteral("Not a subtitle packet");
        return QString();
    }

    if (targetPkt.streamIndex < 0 || targetPkt.streamIndex >= m_reader->streams().size()) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid stream index");
        return QString();
    }

    const StreamInfo &streamInfo = m_reader->streams()[targetPkt.streamIndex];

    // 读取原始数据（通过 PacketReader 的共享 formatCtx，在主线程调用）
    QByteArray rawData = m_reader->readPacketData(packetIndex);
    if (rawData.isEmpty()) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to read packet data");
        return QString();
    }

    const AVCodec *codec = avcodec_find_decoder(streamInfo.codecpar->codec_id);
    if (!codec) {
        if (errorMsg) *errorMsg = QStringLiteral("Subtitle decoder not found");
        return QString();
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to allocate codec context");
        return QString();
    }

    int ret = avcodec_parameters_to_context(codecCtx, streamInfo.codecpar);
    if (ret < 0) {
        avcodec_free_context(&codecCtx);
        if (errorMsg) *errorMsg = ffmpegError(ret);
        return QString();
    }

    ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        avcodec_free_context(&codecCtx);
        if (errorMsg) *errorMsg = ffmpegError(ret);
        return QString();
    }

    AVPacket *pkt = av_packet_alloc();
    pkt->data = reinterpret_cast<uint8_t*>(rawData.data());
    pkt->size = rawData.size();

    AVSubtitle subtitle;
    int gotSub = 0;
    ret = avcodec_decode_subtitle2(codecCtx, &subtitle, &gotSub, pkt);

    QString text;
    if (gotSub) {
        for (unsigned int i = 0; i < subtitle.num_rects; ++i) {
            AVSubtitleRect *rect = subtitle.rects[i];
            if (rect->type == SUBTITLE_TEXT && rect->text) {
                if (!text.isEmpty()) text += QStringLiteral("\n");
                text += QString::fromUtf8(rect->text);
            } else if (rect->type == SUBTITLE_ASS && rect->ass) {
                if (!text.isEmpty()) text += QStringLiteral("\n");
                text += QString::fromUtf8(rect->ass);
            }
        }
        avsubtitle_free(&subtitle);
    } else {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to decode subtitle");
    }

    // 重置 packet data/size 以免 av_packet_free 错误释放
    pkt->data = nullptr;
    pkt->size = 0;
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);

    return text;
}
