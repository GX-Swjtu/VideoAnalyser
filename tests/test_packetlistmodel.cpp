#include <gtest/gtest.h>
#include "packetlistmodel.h"

#include <QBrush>
#include <QColor>

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
                                  int stream = 0, int pictType = -1, bool isIDR = false) {
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
        p.pictType = pictType;
        p.isIDR = isIDR;
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
    EXPECT_EQ(model->headerData(PacketListModel::ColFrame, Qt::Horizontal, Qt::DisplayRole).toString(), "Frame");
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

// ---- 帧类型相关测试 ----

TEST_F(PacketListModelTest, FrameTypeStringStatic) {
    // IDR I帧
    EXPECT_EQ(PacketListModel::frameTypeString(AV_PICTURE_TYPE_I, true), "IDR");
    // Non-IDR I帧
    EXPECT_EQ(PacketListModel::frameTypeString(AV_PICTURE_TYPE_I, false), "I");
    // P帧
    EXPECT_EQ(PacketListModel::frameTypeString(AV_PICTURE_TYPE_P, false), "P");
    // B帧
    EXPECT_EQ(PacketListModel::frameTypeString(AV_PICTURE_TYPE_B, false), "B");
    // 未知
    EXPECT_TRUE(PacketListModel::frameTypeString(-1, false).isEmpty());
    EXPECT_TRUE(PacketListModel::frameTypeString(0, false).isEmpty());
}

TEST_F(PacketListModelTest, FrameTypeColorStatic) {
    // IDR 和 Non-IDR I帧颜色不同
    QColor idrColor = PacketListModel::frameTypeColor(AV_PICTURE_TYPE_I, true);
    QColor nonIdrColor = PacketListModel::frameTypeColor(AV_PICTURE_TYPE_I, false);
    EXPECT_TRUE(idrColor.isValid());
    EXPECT_TRUE(nonIdrColor.isValid());
    EXPECT_NE(idrColor, nonIdrColor);

    // P帧和 B帧颜色有效且不同
    QColor pColor = PacketListModel::frameTypeColor(AV_PICTURE_TYPE_P, false);
    QColor bColor = PacketListModel::frameTypeColor(AV_PICTURE_TYPE_B, false);
    EXPECT_TRUE(pColor.isValid());
    EXPECT_TRUE(bColor.isValid());
    EXPECT_NE(pColor, bColor);

    // 未知帧类型无颜色
    QColor unknownColor = PacketListModel::frameTypeColor(-1, false);
    EXPECT_FALSE(unknownColor.isValid());
}

TEST_F(PacketListModelTest, FrameTypeColorDarkMode) {
    // 暗色模式 IDR 和 Non-IDR I帧颜色不同
    QColor idrDark = PacketListModel::frameTypeColor(AV_PICTURE_TYPE_I, true, true);
    QColor nonIdrDark = PacketListModel::frameTypeColor(AV_PICTURE_TYPE_I, false, true);
    EXPECT_TRUE(idrDark.isValid());
    EXPECT_TRUE(nonIdrDark.isValid());
    EXPECT_NE(idrDark, nonIdrDark);

    // 暗色模式 P帧和 B帧颜色有效且不同
    QColor pDark = PacketListModel::frameTypeColor(AV_PICTURE_TYPE_P, false, true);
    QColor bDark = PacketListModel::frameTypeColor(AV_PICTURE_TYPE_B, false, true);
    EXPECT_TRUE(pDark.isValid());
    EXPECT_TRUE(bDark.isValid());
    EXPECT_NE(pDark, bDark);

    // 暗色模式颜色比亮色模式暗（lightness 更低）
    QColor idrLight = PacketListModel::frameTypeColor(AV_PICTURE_TYPE_I, true, false);
    EXPECT_LT(idrDark.lightness(), idrLight.lightness());

    QColor pLight = PacketListModel::frameTypeColor(AV_PICTURE_TYPE_P, false, false);
    EXPECT_LT(pDark.lightness(), pLight.lightness());

    // 未知帧类型暗色模式也无颜色
    QColor unknownDark = PacketListModel::frameTypeColor(-1, false, true);
    EXPECT_FALSE(unknownDark.isValid());
}

TEST_F(PacketListModelTest, FrameColumnDisplayRole) {
    QVector<PacketInfo> packets;
    // IDR I帧
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, "h264", 0, 1000, 0, AV_PKT_FLAG_KEY, 0,
                              AV_PICTURE_TYPE_I, true));
    // P帧
    packets.append(makePacket(1, AVMEDIA_TYPE_VIDEO, "h264", 40, 500, 1000, 0, 0,
                              AV_PICTURE_TYPE_P, false));
    // B帧
    packets.append(makePacket(2, AVMEDIA_TYPE_VIDEO, "h264", 80, 300, 1500, 0, 0,
                              AV_PICTURE_TYPE_B, false));
    // 音频（无帧类型）
    packets.append(makePacket(3, AVMEDIA_TYPE_AUDIO, "aac", 0, 200, 1800, 0, 1));
    model->setPackets(packets);

    EXPECT_EQ(model->data(model->index(0, PacketListModel::ColFrame), Qt::DisplayRole).toString(), "IDR");
    EXPECT_EQ(model->data(model->index(1, PacketListModel::ColFrame), Qt::DisplayRole).toString(), "P");
    EXPECT_EQ(model->data(model->index(2, PacketListModel::ColFrame), Qt::DisplayRole).toString(), "B");
    EXPECT_TRUE(model->data(model->index(3, PacketListModel::ColFrame), Qt::DisplayRole).toString().isEmpty());
}

TEST_F(PacketListModelTest, FrameColumnBackgroundRole) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, "h264", 0, 1000, 0, AV_PKT_FLAG_KEY, 0,
                              AV_PICTURE_TYPE_I, true));
    packets.append(makePacket(1, AVMEDIA_TYPE_VIDEO, "h264", 40, 500, 1000, 0, 0,
                              AV_PICTURE_TYPE_P, false));
    packets.append(makePacket(2, AVMEDIA_TYPE_VIDEO, "h264", 80, 300, 1500, 0, 0,
                              AV_PICTURE_TYPE_B, false));
    packets.append(makePacket(3, AVMEDIA_TYPE_AUDIO, "aac", 0, 200, 1800, 0, 1));
    model->setPackets(packets);

    // IDR I帧有背景色
    QVariant idrBg = model->data(model->index(0, PacketListModel::ColFrame), Qt::BackgroundRole);
    EXPECT_TRUE(idrBg.isValid());

    // P帧有背景色
    QVariant pBg = model->data(model->index(1, PacketListModel::ColFrame), Qt::BackgroundRole);
    EXPECT_TRUE(pBg.isValid());

    // IDR 和 P 颜色不同
    EXPECT_NE(idrBg.value<QBrush>().color(), pBg.value<QBrush>().color());

    // 音频无帧类型背景色
    QVariant audioBg = model->data(model->index(3, PacketListModel::ColFrame), Qt::BackgroundRole);
    EXPECT_FALSE(audioBg.isValid());
}
