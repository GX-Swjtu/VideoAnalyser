#include <gtest/gtest.h>
#include "avsyncchartwidget.h"
#include "packetreader.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
}

static PacketInfo makePacket(int index, AVMediaType type, double dtsTime,
                              int64_t pos, int stream = 0) {
    PacketInfo p{};
    p.index = index;
    p.streamIndex = stream;
    p.mediaType = type;
    p.dts = static_cast<int64_t>(dtsTime * 1000);
    p.dtsTime = dtsTime;
    p.pts = p.dts;
    p.ptsTime = dtsTime;
    p.pos = pos;
    p.size = 1000;
    p.flags = 0;
    p.duration = 40;
    p.durationTime = 0.04;
    p.gopKeyFrameIndex = -1;
    return p;
}

// ===== interpolateVideoDts 测试 =====

TEST(AVSyncChartWidgetTest, InterpolateVideoDts_Empty) {
    QVector<double> videoTs;
    double result = AVSyncChartWidget::interpolateVideoDts(videoTs, 1.0);
    EXPECT_NEAR(result, 1.0, 0.001); // 返回请求值本身
}

TEST(AVSyncChartWidgetTest, InterpolateVideoDts_BeforeFirst) {
    QVector<double> videoTs = {1.0, 2.0, 3.0};
    double result = AVSyncChartWidget::interpolateVideoDts(videoTs, 0.5);
    EXPECT_NEAR(result, 1.0, 0.001); // 夹到第一个值
}

TEST(AVSyncChartWidgetTest, InterpolateVideoDts_AfterLast) {
    QVector<double> videoTs = {1.0, 2.0, 3.0};
    double result = AVSyncChartWidget::interpolateVideoDts(videoTs, 5.0);
    EXPECT_NEAR(result, 3.0, 0.001); // 夹到最后一个值
}

TEST(AVSyncChartWidgetTest, InterpolateVideoDts_ExactMatch) {
    QVector<double> videoTs = {1.0, 2.0, 3.0};
    double result = AVSyncChartWidget::interpolateVideoDts(videoTs, 2.0);
    EXPECT_NEAR(result, 2.0, 0.001);
}

TEST(AVSyncChartWidgetTest, InterpolateVideoDts_Midpoint) {
    QVector<double> videoTs = {0.0, 1.0, 2.0};
    double result = AVSyncChartWidget::interpolateVideoDts(videoTs, 0.5);
    EXPECT_NEAR(result, 0.5, 0.001); // 线性插值
}

TEST(AVSyncChartWidgetTest, InterpolateVideoDts_QuarterPoint) {
    QVector<double> videoTs = {0.0, 4.0};
    double result = AVSyncChartWidget::interpolateVideoDts(videoTs, 1.0);
    EXPECT_NEAR(result, 1.0, 0.001);
}

// ===== buildSyncDeltaSeries 测试 =====

TEST(AVSyncChartWidgetTest, BuildSyncDeltaSeries_Empty) {
    QVector<PacketInfo> packets;
    auto series = AVSyncChartWidget::buildSyncDeltaSeries(packets, 0, 1, true);
    EXPECT_TRUE(series.isEmpty());
}

TEST(AVSyncChartWidgetTest, BuildSyncDeltaSeries_PerfectSync) {
    // 视频 DTS 和音频 DTS 完全对齐 → delta ≈ 0
    QVector<PacketInfo> packets;
    for (int i = 0; i < 5; ++i) {
        packets.append(makePacket(i * 2,     AVMEDIA_TYPE_VIDEO, i * 0.04, i * 2000, 0));
        packets.append(makePacket(i * 2 + 1, AVMEDIA_TYPE_AUDIO, i * 0.04, i * 2000 + 1000, 1));
    }

    auto series = AVSyncChartWidget::buildSyncDeltaSeries(packets, 0, 1, true);
    EXPECT_EQ(series.size(), 5);
    for (const auto &pt : series) {
        EXPECT_NEAR(pt.y(), 0.0, 1.0); // delta 接近 0 ms
    }
}

TEST(AVSyncChartWidgetTest, BuildSyncDeltaSeries_AudioAhead) {
    // 音频 DTS 比视频快 50ms
    QVector<PacketInfo> packets;
    for (int i = 0; i < 5; ++i) {
        packets.append(makePacket(i * 2,     AVMEDIA_TYPE_VIDEO, i * 0.04,        i * 2000, 0));
        packets.append(makePacket(i * 2 + 1, AVMEDIA_TYPE_AUDIO, i * 0.04 + 0.05, i * 2000 + 1000, 1));
    }

    auto series = AVSyncChartWidget::buildSyncDeltaSeries(packets, 0, 1, true);
    EXPECT_EQ(series.size(), 5);
    for (const auto &pt : series) {
        EXPECT_NEAR(pt.y(), 50.0, 10.0); // delta ≈ +50 ms（音频超前）
    }
}

TEST(AVSyncChartWidgetTest, BuildSyncDeltaSeries_OffsetMode) {
    QVector<PacketInfo> packets;
    // pos = 1MB, 2MB
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, 0.0, 1048576, 0));
    packets.append(makePacket(1, AVMEDIA_TYPE_AUDIO, 0.0, 2097152, 1));

    auto series = AVSyncChartWidget::buildSyncDeltaSeries(packets, 0, 1, true);
    ASSERT_EQ(series.size(), 1);
    EXPECT_NEAR(series[0].x(), 2.0, 0.01); // 音频 pos = 2MB
}

TEST(AVSyncChartWidgetTest, WidgetSetAndClear) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, 0.0, 0, 0));
    packets.append(makePacket(1, AVMEDIA_TYPE_AUDIO, 0.0, 1000, 1));

    QVector<StreamInfo> streams;
    StreamInfo vs{};
    vs.index = 0;
    vs.mediaType = AVMEDIA_TYPE_VIDEO;
    streams.append(vs);
    StreamInfo as{};
    as.index = 1;
    as.mediaType = AVMEDIA_TYPE_AUDIO;
    streams.append(as);

    AVSyncChartWidget widget;
    widget.setPackets(packets, streams);
    widget.clear();
}
