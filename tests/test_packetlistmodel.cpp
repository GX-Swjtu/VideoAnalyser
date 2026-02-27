#include <gtest/gtest.h>
#include "packetlistmodel.h"

#include <QBrush>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

class PacketListModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        model = new PacketListModel();
    }
    void TearDown() override {
        delete model;
    }
    PacketListModel *model = nullptr;

    static PacketInfo makePacket(int index, AVMediaType type, const QString &codec,
                                  int64_t pts, int size, int64_t pos, int flags,
                                  int stream = 0) {
        PacketInfo p;
        p.index = index;
        p.streamIndex = stream;
        p.mediaType = type;
        p.codecName = codec;
        p.pts = pts;
        p.dts = pts;
        p.ptsTime = pts / 1000.0;
        p.dtsTime = pts / 1000.0;
        p.size = size;
        p.pos = pos;
        p.flags = flags;
        p.duration = 40;
        p.durationTime = 0.040;
        p.gopKeyFrameIndex = (type == AVMEDIA_TYPE_VIDEO) ? 0 : -1;
        return p;
    }
};

TEST_F(PacketListModelTest, EmptyModel) {
    EXPECT_EQ(model->rowCount(), 0);
    EXPECT_EQ(model->columnCount(), PacketListModel::ColCount);
}

TEST_F(PacketListModelTest, SetPackets) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, "h264", 0, 1000, 0, AV_PKT_FLAG_KEY));
    packets.append(makePacket(1, AVMEDIA_TYPE_AUDIO, "aac", 0, 200, 1000, 0, 1));
    packets.append(makePacket(2, AVMEDIA_TYPE_VIDEO, "h264", 40, 500, 1200, 0));

    model->setPackets(packets);
    EXPECT_EQ(model->rowCount(), 3);
}

TEST_F(PacketListModelTest, DisplayRoleData) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, "h264", 0, 1000, 0x2B3D2, AV_PKT_FLAG_KEY));
    model->setPackets(packets);

    QModelIndex idx;

    // Index column
    idx = model->index(0, PacketListModel::ColIndex);
    EXPECT_EQ(model->data(idx, Qt::DisplayRole).toInt(), 0);

    // Size column
    idx = model->index(0, PacketListModel::ColSize);
    EXPECT_EQ(model->data(idx, Qt::DisplayRole).toInt(), 1000);

    // Codec column
    idx = model->index(0, PacketListModel::ColCodec);
    EXPECT_EQ(model->data(idx, Qt::DisplayRole).toString(), "h264");
}

TEST_F(PacketListModelTest, BackgroundRoleColor) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, "h264", 0, 100, 0, 0));
    packets.append(makePacket(1, AVMEDIA_TYPE_AUDIO, "aac", 0, 100, 0, 0, 1));
    model->setPackets(packets);

    QModelIndex videoIdx = model->index(0, PacketListModel::ColType);
    QVariant videoBg = model->data(videoIdx, Qt::BackgroundRole);
    EXPECT_TRUE(videoBg.isValid());

    QModelIndex audioIdx = model->index(1, PacketListModel::ColType);
    QVariant audioBg = model->data(audioIdx, Qt::BackgroundRole);
    EXPECT_TRUE(audioBg.isValid());

    // 视频和音频颜色应该不同
    EXPECT_NE(videoBg.value<QBrush>().color(), audioBg.value<QBrush>().color());
}

TEST_F(PacketListModelTest, HeaderData) {
    EXPECT_EQ(model->headerData(PacketListModel::ColType, Qt::Horizontal, Qt::DisplayRole).toString(), "Type");
    EXPECT_EQ(model->headerData(PacketListModel::ColIndex, Qt::Horizontal, Qt::DisplayRole).toString(), "Index");
    EXPECT_EQ(model->headerData(PacketListModel::ColStream, Qt::Horizontal, Qt::DisplayRole).toString(), "Stream");
    EXPECT_EQ(model->headerData(PacketListModel::ColOffset, Qt::Horizontal, Qt::DisplayRole).toString(), "Offset");
    EXPECT_EQ(model->headerData(PacketListModel::ColSize, Qt::Horizontal, Qt::DisplayRole).toString(), "Size");
    EXPECT_EQ(model->headerData(PacketListModel::ColFlags, Qt::Horizontal, Qt::DisplayRole).toString(), "Flags");
    EXPECT_EQ(model->headerData(PacketListModel::ColCodec, Qt::Horizontal, Qt::DisplayRole).toString(), "Codec");
    EXPECT_EQ(model->headerData(PacketListModel::ColPTS, Qt::Horizontal, Qt::DisplayRole).toString(), "PTS");
    EXPECT_EQ(model->headerData(PacketListModel::ColDTS, Qt::Horizontal, Qt::DisplayRole).toString(), "DTS");
    EXPECT_EQ(model->headerData(PacketListModel::ColDuration, Qt::Horizontal, Qt::DisplayRole).toString(), "Duration");
}

TEST_F(PacketListModelTest, TimeFormatting) {
    // 40.040 seconds -> "00:00:40.040"
    EXPECT_EQ(PacketListModel::formatTime(40.040), "00:00:40.040");

    // 0 seconds
    EXPECT_EQ(PacketListModel::formatTime(0.0), "00:00:00.000");

    // Negative → N/A
    EXPECT_EQ(PacketListModel::formatTime(-1.0), "N/A");

    // 1 hour 23 min 45.678 sec
    EXPECT_EQ(PacketListModel::formatTime(5025.678), "01:23:45.678");
}

TEST_F(PacketListModelTest, OffsetFormatting) {
    EXPECT_EQ(PacketListModel::formatOffset(0x2B3D2), "0x0002B3D2");
    EXPECT_EQ(PacketListModel::formatOffset(0), "0x00000000");
    EXPECT_EQ(PacketListModel::formatOffset(-1), "N/A");
}

TEST_F(PacketListModelTest, FlagsFormatting) {
    EXPECT_EQ(PacketListModel::formatFlags(AV_PKT_FLAG_KEY), "KEY");
    EXPECT_EQ(PacketListModel::formatFlags(AV_PKT_FLAG_CORRUPT), "CORRUPT");
    EXPECT_EQ(PacketListModel::formatFlags(AV_PKT_FLAG_KEY | AV_PKT_FLAG_CORRUPT), "KEY | CORRUPT");
    EXPECT_EQ(PacketListModel::formatFlags(0), "");
}

TEST_F(PacketListModelTest, FilterByMediaType) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, "h264", 0, 100, 0, 0));
    packets.append(makePacket(1, AVMEDIA_TYPE_AUDIO, "aac", 0, 100, 100, 0, 1));
    packets.append(makePacket(2, AVMEDIA_TYPE_VIDEO, "h264", 40, 100, 200, 0));
    model->setPackets(packets);

    PacketFilterProxyModel proxy;
    proxy.setSourceModel(model);

    // 全部
    EXPECT_EQ(proxy.rowCount(), 3);

    // 仅视频
    proxy.setMediaTypeFilter(AVMEDIA_TYPE_VIDEO);
    EXPECT_EQ(proxy.rowCount(), 2);

    // 仅音频
    proxy.setMediaTypeFilter(AVMEDIA_TYPE_AUDIO);
    EXPECT_EQ(proxy.rowCount(), 1);

    // 字幕（无）
    proxy.setMediaTypeFilter(AVMEDIA_TYPE_SUBTITLE);
    EXPECT_EQ(proxy.rowCount(), 0);

    // 重置
    proxy.setMediaTypeFilter(-1);
    EXPECT_EQ(proxy.rowCount(), 3);
}

TEST_F(PacketListModelTest, FilterByStream) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, "h264", 0, 100, 0, 0, 0));
    packets.append(makePacket(1, AVMEDIA_TYPE_AUDIO, "aac", 0, 100, 100, 0, 1));
    packets.append(makePacket(2, AVMEDIA_TYPE_VIDEO, "h264", 40, 100, 200, 0, 0));
    model->setPackets(packets);

    PacketFilterProxyModel proxy;
    proxy.setSourceModel(model);

    proxy.setStreamIndexFilter(0);
    EXPECT_EQ(proxy.rowCount(), 2);

    proxy.setStreamIndexFilter(1);
    EXPECT_EQ(proxy.rowCount(), 1);

    proxy.setStreamIndexFilter(-1);
    EXPECT_EQ(proxy.rowCount(), 3);
}
