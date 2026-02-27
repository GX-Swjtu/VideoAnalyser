#include <gtest/gtest.h>
#include "mediainfowidget.h"
#include "packetreader.h"

#include <QFileInfo>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

// ===== 静态方法测试（纯数据，不需要文件） =====

TEST(MediaInfoWidgetTest, FormatFileSize_Zero) {
    EXPECT_EQ(MediaInfoWidget::formatFileSize(0), "0.000 MB");
}

TEST(MediaInfoWidgetTest, FormatFileSize_Small) {
    // 1 MB = 1048576 bytes
    EXPECT_EQ(MediaInfoWidget::formatFileSize(1048576), "1.000 MB");
}

TEST(MediaInfoWidgetTest, FormatFileSize_Large) {
    // 170.868 MB
    int64_t bytes = static_cast<int64_t>(170.868 * 1024 * 1024);
    QString result = MediaInfoWidget::formatFileSize(bytes);
    EXPECT_TRUE(result.contains("MB"));
    // 应接近 170.868
    double val = result.split(' ').first().toDouble();
    EXPECT_NEAR(val, 170.868, 0.01);
}

TEST(MediaInfoWidgetTest, FormatDuration_Normal) {
    // 119 秒 = 00:01:59
    QString result = MediaInfoWidget::formatDuration(119.0);
    EXPECT_TRUE(result.contains("00:01:59"));
    EXPECT_TRUE(result.contains("total 119 s"));
}

TEST(MediaInfoWidgetTest, FormatDuration_Zero) {
    EXPECT_EQ(MediaInfoWidget::formatDuration(0.0), "N/A");
}

TEST(MediaInfoWidgetTest, FormatDuration_Negative) {
    EXPECT_EQ(MediaInfoWidget::formatDuration(-1.0), "N/A");
}

TEST(MediaInfoWidgetTest, FormatBitrate_Mbps) {
    // 11945000 bps → 11.945 Mbps
    QString result = MediaInfoWidget::formatBitrate(11945000);
    EXPECT_TRUE(result.contains("Mbps"));
    double val = result.split(' ').first().toDouble();
    EXPECT_NEAR(val, 11.945, 0.001);
}

TEST(MediaInfoWidgetTest, FormatBitrate_Kbps) {
    // 125000 bps → 125 Kbps
    QString result = MediaInfoWidget::formatBitrate(125000);
    EXPECT_TRUE(result.contains("Kbps"));
}

TEST(MediaInfoWidgetTest, FormatBitrate_Zero) {
    EXPECT_EQ(MediaInfoWidget::formatBitrate(0), "0 Kbps");
}

TEST(MediaInfoWidgetTest, FormatAspectRatio_16_9) {
    EXPECT_EQ(MediaInfoWidget::formatAspectRatio(1920, 1080), "[16:9]");
}

TEST(MediaInfoWidgetTest, FormatAspectRatio_1_1) {
    EXPECT_EQ(MediaInfoWidget::formatAspectRatio(1, 1), "[1:1]");
}

TEST(MediaInfoWidgetTest, FormatAspectRatio_Zero) {
    EXPECT_EQ(MediaInfoWidget::formatAspectRatio(0, 0), "[0:0]");
}

TEST(MediaInfoWidgetTest, ComputeAverageGopSize_Normal) {
    // 模拟 GOP: K P P P K P P P K
    QVector<PacketInfo> packets;
    for (int i = 0; i < 9; ++i) {
        PacketInfo p{};
        p.index = i;
        p.streamIndex = 0;
        p.mediaType = AVMEDIA_TYPE_VIDEO;
        p.flags = (i % 4 == 0) ? AV_PKT_FLAG_KEY : 0;
        packets.append(p);
    }
    // 关键帧在 0,4,8 → GOP 大小 4,4 → 平均 4
    double avg = MediaInfoWidget::computeAverageGopSize(packets, 0);
    EXPECT_NEAR(avg, 4.0, 0.01);
}

TEST(MediaInfoWidgetTest, ComputeAverageGopSize_NoKeyFrames) {
    QVector<PacketInfo> packets;
    for (int i = 0; i < 5; ++i) {
        PacketInfo p{};
        p.index = i;
        p.streamIndex = 0;
        p.mediaType = AVMEDIA_TYPE_VIDEO;
        p.flags = 0; // 无关键帧
        packets.append(p);
    }
    EXPECT_NEAR(MediaInfoWidget::computeAverageGopSize(packets, 0), 0.0, 0.01);
}

TEST(MediaInfoWidgetTest, ComputeAverageGopSize_IgnoresOtherStreams) {
    QVector<PacketInfo> packets;
    // 视频流 0: K P K
    for (int i = 0; i < 3; ++i) {
        PacketInfo p{};
        p.index = i;
        p.streamIndex = 0;
        p.mediaType = AVMEDIA_TYPE_VIDEO;
        p.flags = (i % 2 == 0) ? AV_PKT_FLAG_KEY : 0;
        packets.append(p);
    }
    // 音频流 1（不应影响）
    for (int i = 3; i < 6; ++i) {
        PacketInfo p{};
        p.index = i;
        p.streamIndex = 1;
        p.mediaType = AVMEDIA_TYPE_AUDIO;
        p.flags = 0;
        packets.append(p);
    }
    // 关键帧在帧0, 帧2 → GOP 大小 2
    EXPECT_NEAR(MediaInfoWidget::computeAverageGopSize(packets, 0), 2.0, 0.01);
}

// ===== 使用真实文件的集成测试 =====

static QString testFile(const char *name) {
    return QString::fromUtf8(TEST_DATA_DIR) + "/" + name;
}

TEST(MediaInfoWidgetTest, SetMediaInfo_WithRealFile) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFileInfo::exists(path))
        GTEST_SKIP() << "Test file not found: " << path.toStdString();

    PacketReader reader;
    ASSERT_TRUE(reader.open(path));
    ASSERT_TRUE(reader.readAllPackets());

    MediaInfoWidget widget;
    // 不应崩溃
    widget.setMediaInfo(path, reader.streams(), reader.formatContext(), reader.packets());
    widget.clear();

    reader.close();
}
