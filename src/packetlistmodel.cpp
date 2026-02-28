#include "packetlistmodel.h"

#include <QBrush>
#include <QColor>
#include <QTime>

extern "C" {
#include <libavutil/avutil.h>
}

// ---- PacketListModel ----

PacketListModel::PacketListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void PacketListModel::setPackets(const QVector<PacketInfo> &packets)
{
    beginResetModel();
    m_packets = packets;
    endResetModel();
}

void PacketListModel::clear()
{
    beginResetModel();
    m_packets.clear();
    endResetModel();
}

int PacketListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_packets.size();
}

int PacketListModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return ColCount;
}

const PacketInfo *PacketListModel::packetAtRow(int row) const
{
    if (row < 0 || row >= m_packets.size())
        return nullptr;
    return &m_packets.at(row);
}

QString PacketListModel::formatTime(double seconds)
{
    if (seconds < 0) return QStringLiteral("N/A");
    int ms = static_cast<int>(seconds * 1000.0 + 0.5);
    return QTime::fromMSecsSinceStartOfDay(ms).toString(QStringLiteral("HH:mm:ss.zzz"));
}

QString PacketListModel::formatOffset(int64_t pos)
{
    if (pos < 0) return QStringLiteral("N/A");
    return QString::asprintf("0x%08llX", static_cast<unsigned long long>(pos));
}

QString PacketListModel::formatFlags(int flags)
{
    QStringList parts;
    if (flags & AV_PKT_FLAG_KEY)      parts << QStringLiteral("KEY");
    if (flags & AV_PKT_FLAG_CORRUPT)  parts << QStringLiteral("CORRUPT");
    if (flags & AV_PKT_FLAG_DISCARD)  parts << QStringLiteral("DISCARD");
    return parts.join(QStringLiteral(" | "));
}

QString PacketListModel::mediaTypeString(AVMediaType type)
{
    const char *name = av_get_media_type_string(type);
    return name ? QString::fromUtf8(name) : QStringLiteral("unknown");
}

QString PacketListModel::mediaTypeIcon(AVMediaType type)
{
    switch (type) {
    case AVMEDIA_TYPE_VIDEO:    return QStringLiteral("\U0001F3AC"); // 🎬
    case AVMEDIA_TYPE_AUDIO:    return QStringLiteral("\U0001F50A"); // 🔊
    case AVMEDIA_TYPE_SUBTITLE: return QStringLiteral("\U0001F4DD"); // 📝
    default:                    return QStringLiteral("\U0001F4E6"); // 📦
    }
}

QVariant PacketListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_packets.size())
        return QVariant();

    const PacketInfo &pkt = m_packets.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColType:     return mediaTypeIcon(pkt.mediaType);
        case ColIndex:    return pkt.index;
        case ColStream:   return pkt.streamIndex;
        case ColOffset:   return formatOffset(pkt.pos);
        case ColSize:     return pkt.size;
        case ColFlags:    return formatFlags(pkt.flags);
        case ColCodec:    return pkt.codecName;
        case ColPTS: {
            QString timeStr = formatTime(pkt.ptsTime);
            if (pkt.pts != AV_NOPTS_VALUE)
                return QStringLiteral("%1 (%2)").arg(timeStr).arg(pkt.pts);
            return QStringLiteral("N/A");
        }
        case ColDTS: {
            QString timeStr = formatTime(pkt.dtsTime);
            if (pkt.dts != AV_NOPTS_VALUE)
                return QStringLiteral("%1 (%2)").arg(timeStr).arg(pkt.dts);
            return QStringLiteral("N/A");
        }
        case ColDuration:
            return QString::number(pkt.durationTime, 'f', 3);
        }
    }

    // 排序用的原始数值
    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColType:     return static_cast<int>(pkt.mediaType);
        case ColIndex:    return pkt.index;
        case ColStream:   return pkt.streamIndex;
        case ColOffset:   return static_cast<qlonglong>(pkt.pos);
        case ColSize:     return pkt.size;
        case ColFlags:    return pkt.flags;
        case ColCodec:    return pkt.codecName;
        case ColPTS:      return pkt.ptsTime;
        case ColDTS:      return pkt.dtsTime;
        case ColDuration: return pkt.durationTime;
        }
    }

    if (role == Qt::BackgroundRole && index.column() == ColType) {
        switch (pkt.mediaType) {
        case AVMEDIA_TYPE_VIDEO:    return QBrush(QColor(200, 220, 255));  // 浅蓝
        case AVMEDIA_TYPE_AUDIO:    return QBrush(QColor(200, 255, 200));  // 浅绿
        case AVMEDIA_TYPE_SUBTITLE: return QBrush(QColor(255, 255, 200));  // 浅黄
        default:                    return QBrush(QColor(230, 230, 230));  // 浅灰
        }
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColIndex:
        case ColStream:
        case ColSize:
        case ColDuration:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        case ColOffset:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    return QVariant();
}

QVariant PacketListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    switch (section) {
    case ColType:     return QStringLiteral("Type");
    case ColIndex:    return QStringLiteral("Index");
    case ColStream:   return QStringLiteral("Stream");
    case ColOffset:   return QStringLiteral("Offset");
    case ColSize:     return QStringLiteral("Size");
    case ColFlags:    return QStringLiteral("Flags");
    case ColCodec:    return QStringLiteral("Codec");
    case ColPTS:      return QStringLiteral("PTS");
    case ColDTS:      return QStringLiteral("DTS");
    case ColDuration: return QStringLiteral("Duration");
    }
    return QVariant();
}

// ---- PacketFilterProxyModel ----

PacketFilterProxyModel::PacketFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSortRole(Qt::UserRole);
}

void PacketFilterProxyModel::setMediaTypeFilter(int type)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    m_mediaTypeFilter = type;
    endFilterChange();
#else
    m_mediaTypeFilter = type;
    invalidateFilter();
#endif
}

void PacketFilterProxyModel::setStreamIndexFilter(int streamIndex)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    m_streamIndexFilter = streamIndex;
    endFilterChange();
#else
    m_streamIndexFilter = streamIndex;
    invalidateFilter();
#endif
}

bool PacketFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    auto *model = qobject_cast<PacketListModel*>(sourceModel());
    if (!model) return true;

    const PacketInfo *pkt = model->packetAtRow(sourceRow);
    if (!pkt) return false;

    if (m_mediaTypeFilter >= 0) {
        if (static_cast<int>(pkt->mediaType) != m_mediaTypeFilter)
            return false;
    }

    if (m_streamIndexFilter >= 0) {
        if (pkt->streamIndex != m_streamIndexFilter)
            return false;
    }

    return true;
}
