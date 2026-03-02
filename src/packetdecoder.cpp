#include "packetdecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
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

    av_log(NULL, AV_LOG_WARNING, "[VDecode] === START === pktIdx=%d codec=%s codec_id=%d targetPTS=%lld targetDTS=%lld flags=%d gopKeyFrame=%d\n",
           packetIndex, streamInfo.codecName.toUtf8().constData(),
           streamInfo.codecpar->codec_id,
           (long long)targetPkt.pts, (long long)targetPkt.dts,
           targetPkt.flags, targetPkt.gopKeyFrameIndex);

    // 找到解码器
    const AVCodec *codec = avcodec_find_decoder(streamInfo.codecpar->codec_id);
    if (!codec) {
        av_log(NULL, AV_LOG_ERROR, "[VDecode] FAIL: avcodec_find_decoder returned NULL for codec_id=%d\n", streamInfo.codecpar->codec_id);
        if (errorMsg) *errorMsg = QStringLiteral("Decoder not found for codec: %1").arg(streamInfo.codecName);
        return QImage();
    }
    av_log(NULL, AV_LOG_WARNING, "[VDecode] Found decoder: %s (%s)\n", codec->name, codec->long_name);

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to allocate codec context");
        return QImage();
    }

    int ret = avcodec_parameters_to_context(codecCtx, streamInfo.codecpar);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "[VDecode] FAIL: avcodec_parameters_to_context: %s\n", ffmpegError(ret).toUtf8().constData());
        if (errorMsg) *errorMsg = QStringLiteral("Failed to copy codec params: %1").arg(ffmpegError(ret));
        avcodec_free_context(&codecCtx);
        return QImage();
    }
    av_log(NULL, AV_LOG_WARNING, "[VDecode] codecCtx: w=%d h=%d pix_fmt=%d extradata_size=%d\n",
           codecCtx->width, codecCtx->height, codecCtx->pix_fmt, codecCtx->extradata_size);

    codecCtx->pkt_timebase = streamInfo.timeBase;

    ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "[VDecode] FAIL: avcodec_open2: %s\n", ffmpegError(ret).toUtf8().constData());
        if (errorMsg) *errorMsg = QStringLiteral("Failed to open decoder: %1").arg(ffmpegError(ret));
        avcodec_free_context(&codecCtx);
        return QImage();
    }
    av_log(NULL, AV_LOG_WARNING, "[VDecode] Decoder opened OK\n");

    // 找到 GOP 起始关键帧
    int gopStart = m_reader->findGopKeyFrame(packetIndex);
    if (gopStart < 0) gopStart = 0; // 找不到就从头开始

    const PacketInfo &gopPkt = m_reader->packetAt(gopStart);

    // 针对 "leading picture" 场景：目标帧在显示顺序上早于 GOP 关键帧
    // （如 VVC/HEVC 层级 B 帧结构，关键帧的 PTS 大于前面 B 帧的 PTS）。
    // 解码器在 seek / flush 后会跳过 RASL 帧，需从更早的关键帧开始解码。
    bool isLeadingPicture = (targetPkt.pts != AV_NOPTS_VALUE &&
                             gopPkt.pts != AV_NOPTS_VALUE &&
                             targetPkt.pts < gopPkt.pts);
    bool isFirstGopLeading = false; // 首 GOP leading picture（VVC RASL/RADL 帧，无更早关键帧可回退）

    if (isLeadingPicture) {
        bool canFallBack = false;
        if (gopStart > 0) {
            // 使用 findPrevGopKeyFrame 直接在关键帧列表中查找前一个关键帧，
            // 避免 findGopKeyFrame(gopStart-1) 因相邻 packet 是音频包而返回 -1 的问题。
            int prevGopStart = m_reader->findPrevGopKeyFrame(targetPkt.streamIndex, gopStart);
            if (prevGopStart >= 0 && prevGopStart < gopStart) {
                av_log(NULL, AV_LOG_WARNING,
                       "[VDecode] Leading picture detected (targetPTS=%lld < gopPTS=%lld), using prev GOP keyframe %d -> %d\n",
                       (long long)targetPkt.pts, (long long)gopPkt.pts, gopStart, prevGopStart);
                gopStart = prevGopStart;
                canFallBack = true;
            }
        }
        if (!canFallBack) {
            // 首 GOP leading picture：VVC/HEVC 流的起始帧不是 IDR 而是 B 帧的情况。
            // 这些帧（RASL/RADL）在显示顺序上早于首个关键帧（CRA/IDR），
            // 无更早的关键帧可回退。需要从文件最开头开始解码，
            // 并清除 DISCARD 标志以尝试让解码器输出这些帧。
            isFirstGopLeading = true;
            av_log(NULL, AV_LOG_WARNING,
                   "[VDecode] First-GOP leading picture: targetPTS=%lld < keyframePTS=%lld, gopStart=%d, no earlier keyframe available\n",
                   (long long)targetPkt.pts, (long long)gopPkt.pts, gopStart);
        }
    }

    const PacketInfo &seekPkt = m_reader->packetAt(gopStart);

    // Seek 到关键帧位置（复用 m_fmtCtx）
    int64_t seekTs;
    if (isFirstGopLeading) {
        // 首 GOP leading picture：使用 avformat_seek_file 精确 seek 到流最开头，
        // 确保从第一个 packet（通常是 IDR/CRA）开始读取。
        // 避免因 DTS 为负值（如 VVC 的 CRA DTS=-1004）导致 seek 定位异常。
        int64_t startTs = (m_fmtCtx->start_time != AV_NOPTS_VALUE) ? m_fmtCtx->start_time : 0;
        seekTs = startTs;
        ret = avformat_seek_file(m_fmtCtx, -1, INT64_MIN, startTs, startTs, 0);
        av_log(NULL, AV_LOG_WARNING,
               "[VDecode] First-GOP seek to start: startTs=%lld ret=%d\n",
               (long long)startTs, ret);
    } else {
        seekTs = (seekPkt.dts != AV_NOPTS_VALUE) ? seekPkt.dts : seekPkt.pts;
        if (seekTs == AV_NOPTS_VALUE) seekTs = 0;
        ret = av_seek_frame(m_fmtCtx, targetPkt.streamIndex, seekTs, AVSEEK_FLAG_BACKWARD);
    }

    av_log(NULL, AV_LOG_WARNING, "[VDecode] GOP seek: gopStart=%d seekTs=%lld streamIndex=%d isLeading=%d isFirstGopLeading=%d\n",
           gopStart, (long long)seekTs, targetPkt.streamIndex, isLeadingPicture ? 1 : 0, isFirstGopLeading ? 1 : 0);

    if (ret < 0) {
        av_log(NULL, AV_LOG_WARNING, "[VDecode] seek failed: %s, trying seek to 0\n", ffmpegError(ret).toUtf8().constData());
        av_seek_frame(m_fmtCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
    }

    // 注意：不调用 avcodec_flush_buffers — codecCtx 刚刚 open，无需 flush。
    // 对 VVC/HEVC 解码器，flush 会触发 RASL 帧跳过机制，导致 leading picture 无法输出。

    // 连续读取直到找到目标帧
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    QImage result;

    bool found = false;
    int maxPackets = 5000; // 防止无限循环

    int sendOkCount = 0, sendFailCount = 0;
    int recvOkCount = 0, recvEagainCount = 0, recvEofCount = 0, recvErrCount = 0;
    int readPktCount = 0;
    int64_t lastRecvPts = AV_NOPTS_VALUE; // 记录最后一个解码出的帧 PTS

    // 最近帧兜底：记录与目标 PTS 差值最小的帧，用于 PTS 不精确匹配时的兜底
    QImage closestImage;
    int64_t closestDelta = INT64_MAX;
    int64_t closestPts = AV_NOPTS_VALUE;

    // 将解码帧转为 QImage 的辅助函数
    auto frameToImage = [&](AVFrame *frm) -> QImage {
        int w = frm->width;
        int h = frm->height;
        AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frm->format);

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
        if (!m_swsCtx) return QImage();

        AVFrame *rgbFrame = av_frame_alloc();
        rgbFrame->format = AV_PIX_FMT_RGB24;
        rgbFrame->width = w;
        rgbFrame->height = h;
        av_frame_get_buffer(rgbFrame, 0);

        sws_scale(m_swsCtx, frm->data, frm->linesize, 0, h,
                  rgbFrame->data, rgbFrame->linesize);

        QImage img(rgbFrame->data[0], rgbFrame->width, rgbFrame->height,
                   rgbFrame->linesize[0], QImage::Format_RGB888);
        QImage copy = img.copy(); // 深拷贝
        av_frame_free(&rgbFrame);
        return copy;
    };

    // 辅助 lambda：从解码器接收帧并匹配目标 PTS
    auto drainFrames = [&]() {
        while (!found) {
            ret = avcodec_receive_frame(codecCtx, frame);
            if (ret == AVERROR(EAGAIN)) { recvEagainCount++; break; }
            if (ret == AVERROR_EOF) { recvEofCount++; break; }
            if (ret < 0) {
                recvErrCount++;
                av_log(NULL, AV_LOG_WARNING, "[VDecode] avcodec_receive_frame error: %s\n",
                       ffmpegError(ret).toUtf8().constData());
                break;
            }
            recvOkCount++;
            lastRecvPts = frame->pts;

            // 获取 best_effort_timestamp（某些解码器如 VVC 的 frame->pts 不一定与包 PTS 一致）
            int64_t bet = frame->best_effort_timestamp;

            av_log(NULL, AV_LOG_WARNING,
                   "[VDecode] Got frame: pts=%lld best_effort_ts=%lld w=%d h=%d fmt=%d (target pts=%lld)\n",
                   (long long)frame->pts, (long long)bet,
                   frame->width, frame->height, frame->format, (long long)targetPkt.pts);

            // PTS 匹配策略：精确 pts → 精确 best_effort_timestamp
            bool ptsMatch = (frame->pts == targetPkt.pts);
            bool betMatch = (!ptsMatch && bet != AV_NOPTS_VALUE && bet == targetPkt.pts);

            if (ptsMatch || betMatch) {
                result = frameToImage(frame);
                found = !result.isNull();
                av_frame_unref(frame);
                break;
            }

            // 记录与目标 PTS 最近的帧，用于兜底
            int64_t effectivePts = (bet != AV_NOPTS_VALUE) ? bet : frame->pts;
            if (effectivePts != AV_NOPTS_VALUE) {
                int64_t delta = std::abs(effectivePts - targetPkt.pts);
                if (delta < closestDelta) {
                    closestDelta = delta;
                    closestPts = effectivePts;
                    closestImage = frameToImage(frame);
                }
            }

            av_frame_unref(frame);
        }
    };

    while (!found && maxPackets-- > 0) {
        ret = av_read_frame(m_fmtCtx, pkt);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "[VDecode] av_read_frame ended: %s after readPktCount=%d\n",
                   ffmpegError(ret).toUtf8().constData(), readPktCount);
            break;
        }

        if (pkt->stream_index != targetPkt.streamIndex) {
            av_packet_unref(pkt);
            continue;
        }

        readPktCount++;
        // 前 5 个包和目标附近的包打详细日志
        if (readPktCount <= 5 || (pkt->pts >= targetPkt.pts - 3000 && pkt->pts <= targetPkt.pts + 3000)) {
            av_log(NULL, AV_LOG_WARNING, "[VDecode] Read pkt #%d pts=%lld dts=%lld size=%d flags=%d\n",
                   readPktCount, (long long)pkt->pts, (long long)pkt->dts, pkt->size, pkt->flags);
        }

        // 首 GOP leading picture：清除 DISCARD 标志。
        // VVC/HEVC 的 RASL/RADL 帧会被 demuxer 标记为 DISCARD，
        // 清除后让解码器有机会尝试解码这些帧（RADL 帧引用 CRA/IDR 之后的帧，理论上可解码）。
        if (isFirstGopLeading && (pkt->flags & AV_PKT_FLAG_DISCARD)) {
            pkt->flags &= ~AV_PKT_FLAG_DISCARD;
            av_log(NULL, AV_LOG_WARNING, "[VDecode] Cleared DISCARD flag on pkt pts=%lld dts=%lld\n",
                   (long long)pkt->pts, (long long)pkt->dts);
        }

        ret = avcodec_send_packet(codecCtx, pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            sendFailCount++;
            if (sendFailCount <= 5) {
                av_log(NULL, AV_LOG_WARNING, "[VDecode] avcodec_send_packet FAIL #%d: %s pkt pts=%lld size=%d\n",
                       sendFailCount, ffmpegError(ret).toUtf8().constData(),
                       (long long)pkt->pts, pkt->size);
            }
            av_packet_unref(pkt);
            continue;
        }
        sendOkCount++;

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
        av_log(NULL, AV_LOG_WARNING, "[VDecode] Draining decoder (not found yet)...\n");
        avcodec_send_packet(codecCtx, nullptr);
        drainFrames();
    }

    // 兜底：精确 PTS / best_effort_ts 均未匹配时，使用与目标最近的帧
    if (!found && !closestImage.isNull()) {
        int64_t tolerance;
        if (isFirstGopLeading) {
            // 首 GOP leading picture（RASL 帧）：解码器可能无法输出精确帧。
            // 使用更大容差：目标 PTS 到关键帧 PTS 的距离 + 3 个 duration，
            // 允许回退到最近可解码帧（通常为 CRA/IDR 本身）。
            int64_t gopRange = std::abs(gopPkt.pts - targetPkt.pts);
            tolerance = gopRange + ((targetPkt.duration > 0) ? targetPkt.duration * 3 : 3003);
            av_log(NULL, AV_LOG_WARNING,
                   "[VDecode] First-GOP leading: using extended tolerance=%lld (gopRange=%lld)\n",
                   (long long)tolerance, (long long)gopRange);
        } else {
            // 普通情况：≤ 3 个 duration（覆盖 VVC 等解码器可能的时间戳偏差）
            tolerance = (targetPkt.duration > 0) ? targetPkt.duration * 3 : 3003;
        }
        if (closestDelta <= tolerance) {
            av_log(NULL, AV_LOG_WARNING,
                   "[VDecode] Using closest frame as fallback: closestPts=%lld delta=%lld tolerance=%lld\n",
                   (long long)closestPts, (long long)closestDelta, (long long)tolerance);
            result = closestImage;
            found = true;
        } else {
            av_log(NULL, AV_LOG_WARNING,
                   "[VDecode] Closest frame too far: closestPts=%lld delta=%lld tolerance=%lld\n",
                   (long long)closestPts, (long long)closestDelta, (long long)tolerance);
        }
    }

    av_log(NULL, AV_LOG_WARNING,
           "[VDecode] === END === found=%d readPkt=%d sendOk=%d sendFail=%d recvOk=%d recvEagain=%d recvEof=%d recvErr=%d lastRecvPts=%lld closestPts=%lld closestDelta=%lld\n",
           found ? 1 : 0, readPktCount, sendOkCount, sendFailCount,
           recvOkCount, recvEagainCount, recvEofCount, recvErrCount,
           (long long)lastRecvPts, (long long)closestPts, (long long)closestDelta);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);

    if (!found && errorMsg) {
        if (isFirstGopLeading) {
            *errorMsg = QStringLiteral("无法解码此帧：该帧是首 GOP 的 leading picture（RASL 帧），"
                                       "位于首个关键帧（PTS=%1）之前，解码器可能无法输出。"
                                       "\n（readPkt=%2 sendOk=%3 sendFail=%4 recvOk=%5 recvErr=%6 closestDelta=%7）")
                .arg(gopPkt.pts).arg(readPktCount).arg(sendOkCount).arg(sendFailCount)
                .arg(recvOkCount).arg(recvErrCount).arg(closestDelta);
        } else {
            *errorMsg = QStringLiteral("Failed to decode target frame (PTS=%1) | readPkt=%2 sendOk=%3 sendFail=%4 recvOk=%5 recvErr=%6 lastRecvPts=%7 closestPts=%8 closestDelta=%9")
                .arg(targetPkt.pts).arg(readPktCount).arg(sendOkCount).arg(sendFailCount)
                .arg(recvOkCount).arg(recvErrCount).arg(lastRecvPts).arg(closestPts).arg(closestDelta);
        }
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
