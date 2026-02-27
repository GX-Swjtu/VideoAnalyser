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

QString PacketDecoder::ffmpegError(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

QImage PacketDecoder::decodeVideoPacket(PacketReader *reader, int packetIndex, QString *errorMsg)
{
    if (!reader || packetIndex < 0 || packetIndex >= reader->packetCount()) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid packet index");
        return QImage();
    }

    const PacketInfo &targetPkt = reader->packetAt(packetIndex);
    if (targetPkt.mediaType != AVMEDIA_TYPE_VIDEO) {
        if (errorMsg) *errorMsg = QStringLiteral("Not a video packet");
        return QImage();
    }

    if (targetPkt.streamIndex < 0 || targetPkt.streamIndex >= reader->streams().size()) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid stream index");
        return QImage();
    }

    const StreamInfo &streamInfo = reader->streams()[targetPkt.streamIndex];

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
    int gopStart = reader->findGopKeyFrame(packetIndex);
    if (gopStart < 0) gopStart = 0; // 找不到就从头开始

    const PacketInfo &gopPkt = reader->packetAt(gopStart);

    // 打开独立的 AVFormatContext 避免与 reader 共享状态
    AVFormatContext *fmtCtx = nullptr;
    {
        QByteArray pathUtf8 = reader->filePath().toUtf8();
        ret = avformat_open_input(&fmtCtx, pathUtf8.constData(), nullptr, nullptr);
        if (ret < 0) {
            if (errorMsg) *errorMsg = QStringLiteral("Failed to open file for decoding: %1").arg(ffmpegError(ret));
            avcodec_free_context(&codecCtx);
            return QImage();
        }
        ret = avformat_find_stream_info(fmtCtx, nullptr);
        if (ret < 0) {
            if (errorMsg) *errorMsg = QStringLiteral("Failed to find stream info: %1").arg(ffmpegError(ret));
            avformat_close_input(&fmtCtx);
            avcodec_free_context(&codecCtx);
            return QImage();
        }
    }

    // Seek 到关键帧位置
    int64_t seekTs = (gopPkt.dts != AV_NOPTS_VALUE) ? gopPkt.dts : gopPkt.pts;
    if (seekTs == AV_NOPTS_VALUE) seekTs = 0;

    ret = av_seek_frame(fmtCtx, targetPkt.streamIndex, seekTs, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        // 尝试 seek 到文件开头
        av_seek_frame(fmtCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
    }

    avcodec_flush_buffers(codecCtx);

    // 连续读取直到找到目标帧
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    QImage result;

    bool found = false;
    int maxPackets = 5000; // 防止无限循环

    while (!found && maxPackets-- > 0) {
        ret = av_read_frame(fmtCtx, pkt);
        if (ret < 0) break;

        if (pkt->stream_index != targetPkt.streamIndex) {
            av_packet_unref(pkt);
            continue;
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

            // PTS 匹配
            if (frame->pts == targetPkt.pts) {
                // 转换为 RGB24
                SwsContext *swsCtx = sws_getContext(
                    frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                    frame->width, frame->height, AV_PIX_FMT_RGB24,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);

                if (swsCtx) {
                    AVFrame *rgbFrame = av_frame_alloc();
                    rgbFrame->format = AV_PIX_FMT_RGB24;
                    rgbFrame->width = frame->width;
                    rgbFrame->height = frame->height;
                    av_frame_get_buffer(rgbFrame, 0);

                    sws_scale(swsCtx, frame->data, frame->linesize, 0, frame->height,
                              rgbFrame->data, rgbFrame->linesize);

                    QImage img(rgbFrame->data[0], rgbFrame->width, rgbFrame->height,
                               rgbFrame->linesize[0], QImage::Format_RGB888);
                    result = img.copy(); // 深拷贝

                    av_frame_free(&rgbFrame);
                    sws_freeContext(swsCtx);
                }

                found = true;
                av_frame_unref(frame);
                break;
            }

            av_frame_unref(frame);
        }

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

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    if (!found && errorMsg) {
        *errorMsg = QStringLiteral("Failed to decode target frame (PTS=%1)").arg(targetPkt.pts);
    }

    return result;
}

AudioData PacketDecoder::decodeAudioPacket(PacketReader *reader, int packetIndex, QString *errorMsg)
{
    AudioData audioData;

    if (!reader || packetIndex < 0 || packetIndex >= reader->packetCount()) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid packet index");
        return audioData;
    }

    const PacketInfo &targetPkt = reader->packetAt(packetIndex);
    if (targetPkt.mediaType != AVMEDIA_TYPE_AUDIO) {
        if (errorMsg) *errorMsg = QStringLiteral("Not an audio packet");
        return audioData;
    }

    if (targetPkt.streamIndex < 0 || targetPkt.streamIndex >= reader->streams().size()) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid stream index");
        return audioData;
    }

    const StreamInfo &streamInfo = reader->streams()[targetPkt.streamIndex];

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

    // 打开独立的 AVFormatContext 避免与 reader 共享状态
    AVFormatContext *fmtCtx = nullptr;
    {
        QByteArray pathUtf8 = reader->filePath().toUtf8();
        ret = avformat_open_input(&fmtCtx, pathUtf8.constData(), nullptr, nullptr);
        if (ret < 0) {
            if (errorMsg) *errorMsg = QStringLiteral("Failed to open file for decoding: %1").arg(ffmpegError(ret));
            avcodec_free_context(&codecCtx);
            return audioData;
        }
        ret = avformat_find_stream_info(fmtCtx, nullptr);
        if (ret < 0) {
            if (errorMsg) *errorMsg = QStringLiteral("Failed to find stream info: %1").arg(ffmpegError(ret));
            avformat_close_input(&fmtCtx);
            avcodec_free_context(&codecCtx);
            return audioData;
        }
    }

    // Seek 到目标 Packet 附近
    int64_t seekTs = (targetPkt.dts != AV_NOPTS_VALUE) ? targetPkt.dts : targetPkt.pts;
    if (seekTs == AV_NOPTS_VALUE) seekTs = 0;

    av_seek_frame(fmtCtx, targetPkt.streamIndex, seekTs, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecCtx);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool found = false;
    bool sentTarget = false;
    int maxPackets = 5000;

    while (!found && maxPackets-- > 0) {
        ret = av_read_frame(fmtCtx, pkt);
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
                int nbSamples = frame->nb_samples;
                int channels = frame->ch_layout.nb_channels;

                audioData.sampleRate = frame->sample_rate;
                audioData.channels = channels;

                AVSampleFormat fmt = static_cast<AVSampleFormat>(frame->format);

                if (fmt == AV_SAMPLE_FMT_FLTP) {
                    // 最常见情况：AAC 输出 planar float，直接交错拷贝
                    audioData.samples.resize(nbSamples * channels);
                    for (int ch = 0; ch < channels; ++ch) {
                        const float *src = reinterpret_cast<const float*>(frame->extended_data[ch]);
                        for (int i = 0; i < nbSamples; ++i) {
                            audioData.samples[i * channels + ch] = src[i];
                        }
                    }
                } else if (fmt == AV_SAMPLE_FMT_FLT && !av_sample_fmt_is_planar(fmt)) {
                    // 已经是 float interleaved
                    audioData.samples.resize(nbSamples * channels);
                    memcpy(audioData.samples.data(), frame->data[0], nbSamples * channels * sizeof(float));
                } else {
                    // 其他格式：使用 libswresample 转换为 float 交错格式
                    SwrContext *swr = nullptr;
                    AVChannelLayout outLayout;
                    av_channel_layout_copy(&outLayout, &frame->ch_layout);

                    ret = swr_alloc_set_opts2(&swr,
                                              &outLayout, AV_SAMPLE_FMT_FLT, frame->sample_rate,
                                              &frame->ch_layout, fmt, frame->sample_rate,
                                              0, nullptr);
                    av_channel_layout_uninit(&outLayout);

                    if (ret >= 0 && swr) {
                        ret = swr_init(swr);
                        if (ret >= 0) {
                            audioData.samples.resize(nbSamples * channels);
                            uint8_t *outBuf = reinterpret_cast<uint8_t*>(audioData.samples.data());
                            int converted = swr_convert(swr, &outBuf, nbSamples,
                                        const_cast<const uint8_t**>(frame->extended_data), nbSamples);
                            if (converted <= 0) {
                                qWarning() << "swr_convert failed or produced 0 samples:" << converted;
                                audioData.samples.clear();
                            }
                        } else {
                            qWarning() << "swr_init failed:" << ffmpegError(ret);
                        }
                        swr_free(&swr);
                    } else {
                        qWarning() << "swr_alloc_set_opts2 failed";
                    }
                }

                if (!audioData.samples.isEmpty()) {
                    found = true;
                }
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

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    if (!found && errorMsg) {
        *errorMsg = QStringLiteral("Failed to decode target audio packet (PTS=%1)").arg(targetPkt.pts);
    }

    return audioData;
}

QString PacketDecoder::decodeSubtitlePacket(PacketReader *reader, int packetIndex, QString *errorMsg)
{
    if (!reader || packetIndex < 0 || packetIndex >= reader->packetCount()) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid packet index");
        return QString();
    }

    const PacketInfo &targetPkt = reader->packetAt(packetIndex);
    if (targetPkt.mediaType != AVMEDIA_TYPE_SUBTITLE) {
        if (errorMsg) *errorMsg = QStringLiteral("Not a subtitle packet");
        return QString();
    }

    if (targetPkt.streamIndex < 0 || targetPkt.streamIndex >= reader->streams().size()) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid stream index");
        return QString();
    }

    const StreamInfo &streamInfo = reader->streams()[targetPkt.streamIndex];

    // 读取原始数据
    QByteArray rawData = reader->readPacketData(packetIndex);
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
