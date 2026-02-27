#include "packetreader.h"

#include <algorithm>
#include <QDebug>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/rational.h>
}

static QString ffmpegError(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

PacketReader::PacketReader(QObject *parent)
    : QObject(parent)
{
}

PacketReader::~PacketReader()
{
    close();
}

bool PacketReader::open(const QString &filePath)
{
    close(); // 先关闭之前的

    m_filePath = filePath;
    QByteArray pathUtf8 = filePath.toUtf8();

    int ret = avformat_open_input(&m_formatCtx, pathUtf8.constData(), nullptr, nullptr);
    if (ret < 0) {
        qWarning() << "Failed to open file:" << ffmpegError(ret);
        m_formatCtx = nullptr;
        return false;
    }

    ret = avformat_find_stream_info(m_formatCtx, nullptr);
    if (ret < 0) {
        qWarning() << "Failed to find stream info:" << ffmpegError(ret);
        avformat_close_input(&m_formatCtx);
        return false;
    }

    // 收集流信息
    m_streams.clear();
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; ++i) {
        AVStream *s = m_formatCtx->streams[i];
        StreamInfo si;
        si.index = static_cast<int>(i);
        si.mediaType = s->codecpar->codec_type;
        si.codecName = QString::fromUtf8(avcodec_get_name(s->codecpar->codec_id));
        si.timeBase = s->time_base;
        si.avgFrameRate = s->avg_frame_rate;
        si.duration = s->duration;
        si.nbFrames = s->nb_frames;

        // 深拷贝 codecpar
        si.codecpar = avcodec_parameters_alloc();
        if (!si.codecpar) {
            qWarning() << "Failed to allocate codec parameters";
            close();
            return false;
        }
        ret = avcodec_parameters_copy(si.codecpar, s->codecpar);
        if (ret < 0) {
            qWarning() << "Failed to copy codec parameters:" << ffmpegError(ret);
            avcodec_parameters_free(&si.codecpar);
            close();
            return false;
        }

        // 音频特有
        si.sampleRate = s->codecpar->sample_rate;
        si.channels = s->codecpar->ch_layout.nb_channels;

        // metadata
        AVDictionaryEntry *tag = nullptr;
        tag = av_dict_get(s->metadata, "title", nullptr, 0);
        if (tag) si.title = QString::fromUtf8(tag->value);
        tag = av_dict_get(s->metadata, "language", nullptr, 0);
        if (tag) si.language = QString::fromUtf8(tag->value);

        m_streams.append(si);
    }

    return true;
}

bool PacketReader::readAllPackets()
{
    if (!m_formatCtx) return false;

    m_packets.clear();
    m_keyFrameIndices.clear();

    // 估算总大小（用于进度报告）
    int64_t totalSize = m_formatCtx->pb ? avio_size(m_formatCtx->pb) : 0;

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) return false;

    int index = 0;
    while (av_read_frame(m_formatCtx, pkt) >= 0) {
        PacketInfo info;
        info.index = index;
        info.streamIndex = pkt->stream_index;

        if (pkt->stream_index >= 0 && pkt->stream_index < static_cast<int>(m_formatCtx->nb_streams)) {
            AVStream *s = m_formatCtx->streams[pkt->stream_index];
            info.mediaType = s->codecpar->codec_type;
            info.codecName = QString::fromUtf8(avcodec_get_name(s->codecpar->codec_id));

            double tb = av_q2d(s->time_base);
            info.pts = pkt->pts;
            info.dts = pkt->dts;
            info.ptsTime = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts * tb : -1.0;
            info.dtsTime = (pkt->dts != AV_NOPTS_VALUE) ? pkt->dts * tb : -1.0;
            info.duration = pkt->duration;
            info.durationTime = pkt->duration * tb;
        } else {
            info.mediaType = AVMEDIA_TYPE_UNKNOWN;
            info.pts = pkt->pts;
            info.dts = pkt->dts;
            info.ptsTime = -1.0;
            info.dtsTime = -1.0;
            info.duration = pkt->duration;
            info.durationTime = 0.0;
        }

        info.size = pkt->size;
        info.pos = pkt->pos;
        info.flags = pkt->flags;
        info.gopKeyFrameIndex = -1;

        // 关键帧索引
        if (pkt->flags & AV_PKT_FLAG_KEY) {
            m_keyFrameIndices[pkt->stream_index].append(index);
        }

        // 为视频流 Packet 计算 gopKeyFrameIndex
        if (info.mediaType == AVMEDIA_TYPE_VIDEO) {
            const auto &kfList = m_keyFrameIndices[pkt->stream_index];
            if (!kfList.isEmpty()) {
                // 找到 ≤ 当前 index 的最大关键帧序号
                auto it = std::upper_bound(kfList.begin(), kfList.end(), index);
                if (it != kfList.begin()) {
                    --it;
                    info.gopKeyFrameIndex = *it;
                }
            }
        }

        m_packets.append(info);
        av_packet_unref(pkt);

        ++index;

        // 发射进度（每 1000 个 Packet）
        if (index % 1000 == 0) {
            if (totalSize > 0 && info.pos >= 0) {
                emit progressChanged(static_cast<int>(info.pos * 100 / totalSize), 100);
            } else {
                emit progressChanged(index, 0);
            }
        }
    }

    av_packet_free(&pkt);

    emit progressChanged(100, 100);
    return true;
}

void PacketReader::close()
{
    m_packets.clear();
    m_keyFrameIndices.clear();

    for (auto &si : m_streams) {
        if (si.codecpar) {
            avcodec_parameters_free(&si.codecpar);
        }
    }
    m_streams.clear();

    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }

    m_filePath.clear();
}

int PacketReader::packetCount() const
{
    return m_packets.size();
}

const PacketInfo &PacketReader::packetAt(int index) const
{
    return m_packets.at(index);
}

const QVector<PacketInfo> &PacketReader::packets() const
{
    return m_packets;
}

const QVector<StreamInfo> &PacketReader::streams() const
{
    return m_streams;
}

QString PacketReader::formatName() const
{
    if (m_formatCtx && m_formatCtx->iformat)
        return QString::fromUtf8(m_formatCtx->iformat->name);
    return QString();
}

double PacketReader::durationSeconds() const
{
    if (m_formatCtx && m_formatCtx->duration != AV_NOPTS_VALUE)
        return static_cast<double>(m_formatCtx->duration) / AV_TIME_BASE;
    return 0.0;
}

int64_t PacketReader::bitRate() const
{
    if (m_formatCtx)
        return m_formatCtx->bit_rate;
    return 0;
}

QByteArray PacketReader::readPacketData(int packetIndex)
{
    if (packetIndex < 0 || packetIndex >= m_packets.size())
        return QByteArray();

    if (!m_formatCtx)
        return QByteArray();

    const PacketInfo &target = m_packets.at(packetIndex);

    // Seek 到目标附近
    int64_t seekTs = (target.dts != AV_NOPTS_VALUE) ? target.dts : target.pts;
    if (seekTs == AV_NOPTS_VALUE) seekTs = 0;

    int ret = av_seek_frame(m_formatCtx, target.streamIndex, seekTs, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        // 如果 seek 失败，尝试从文件开头读
        ret = av_seek_frame(m_formatCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
        if (ret < 0)
            return QByteArray();
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt)
        return QByteArray();

    QByteArray result;

    // 循环读取直到找到匹配的 Packet
    while (av_read_frame(m_formatCtx, pkt) >= 0) {
        bool match = (pkt->stream_index == target.streamIndex) &&
                     (pkt->size == target.size);

        // 使用 pos 匹配（如果有）
        if (match && target.pos >= 0) {
            match = (pkt->pos == target.pos);
        } else if (match) {
            // 没有 pos 信息时，用 pts/dts 匹配
            match = (pkt->pts == target.pts && pkt->dts == target.dts);
        }

        if (match) {
            result = QByteArray(reinterpret_cast<const char*>(pkt->data), pkt->size);
            av_packet_unref(pkt);
            break;
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    return result;
}

AVFormatContext *PacketReader::formatContext() const
{
    return m_formatCtx;
}

int PacketReader::findGopKeyFrame(int packetIndex) const
{
    if (packetIndex < 0 || packetIndex >= m_packets.size())
        return -1;

    const PacketInfo &pkt = m_packets.at(packetIndex);
    if (pkt.mediaType != AVMEDIA_TYPE_VIDEO)
        return -1;

    auto it = m_keyFrameIndices.find(pkt.streamIndex);
    if (it == m_keyFrameIndices.end() || it->isEmpty())
        return -1;

    const auto &kfList = it.value();
    // 用 std::upper_bound 找到第一个 > packetIndex 的关键帧，然后-1
    auto ub = std::upper_bound(kfList.begin(), kfList.end(), packetIndex);
    if (ub == kfList.begin())
        return -1; // 没有 ≤ packetIndex 的关键帧

    --ub;
    return *ub;
}
