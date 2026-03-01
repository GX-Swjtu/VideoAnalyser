#ifndef PACKETLISTMODEL_H
#define PACKETLISTMODEL_H

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QVector>
#include "packetreader.h"

class PacketListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColType = 0,
        ColFrame,
        ColIndex,
        ColStream,
        ColOffset,
        ColSize,
        ColFlags,
        ColCodec,
        ColPTS,
        ColDTS,
        ColDuration,
        ColCount
    };

    explicit PacketListModel(QObject *parent = nullptr);

    void setPackets(const QVector<PacketInfo> &packets);
    void clear();

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    const PacketInfo *packetAtRow(int row) const;

    // 工具函数（公开供测试）
    static QString formatTime(double seconds);
    static QString formatOffset(int64_t pos);
    static QString formatFlags(int flags);
    static QString mediaTypeString(AVMediaType type);
    static QString mediaTypeIcon(AVMediaType type);
    static QString frameTypeString(int pictType, bool isIDR);
    static QColor frameTypeColor(int pictType, bool isIDR, bool isDark = false);

private:
    QVector<PacketInfo> m_packets;
};

class PacketFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit PacketFilterProxyModel(QObject *parent = nullptr);

    void setMediaTypeFilter(int type); // -1 = 全部
    void setStreamIndexFilter(int streamIndex); // -1 = 全部

    int mediaTypeFilter() const { return m_mediaTypeFilter; }
    int streamIndexFilter() const { return m_streamIndexFilter; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &parent) const override;

private:
    int m_mediaTypeFilter = -1;   // -1 = 不过滤
    int m_streamIndexFilter = -1; // -1 = 不过滤
};

#endif // PACKETLISTMODEL_H
