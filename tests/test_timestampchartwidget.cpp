#include <gtest/gtest.h>
#include "timestampchartwidget.h"
#include "packetreader.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
}

// 合成 PacketInfo 工厂
static PacketInfo makePacket(int index, AVMediaType type, double dtsTime, int64_t pos,
                              int stream = 0) {
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

TEST(TimestampChartWidgetTest, BuildTimestampSeries_Empty) {
    QVector<PacketInfo> packets;
    auto series = TimestampChartWidget::buildTimestampSeries(packets, AVMEDIA_TYPE_VIDEO, true);
    EXPECT_TRUE(series.isEmpty());
}

TEST(TimestampChartWidgetTest, BuildTimestampSeries_VideoOnly) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, 0.0,   0));
    packets.append(makePacket(1, AVMEDIA_TYPE_AUDIO, 0.0,   1000, 1));
    packets.append(makePacket(2, AVMEDIA_TYPE_VIDEO, 0.04,  2000));
    packets.append(makePacket(3, AVMEDIA_TYPE_AUDIO, 0.023, 3000, 1));

    auto series = TimestampChartWidget::buildTimestampSeries(packets, AVMEDIA_TYPE_VIDEO, true);
    EXPECT_EQ(series.size(), 2);
    // X = pos offset in MB
    EXPECT_NEAR(series[0].x(), 0.0, 0.01);
    EXPECT_NEAR(series[0].y(), 0.0, 0.01);   // DTS=0 ms
    EXPECT_NEAR(series[1].y(), 40.0, 0.01);  // DTS=40 ms
}

TEST(TimestampChartWidgetTest, BuildTimestampSeries_TimeMode) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, 1.0, 0));
    packets.append(makePacket(1, AVMEDIA_TYPE_VIDEO, 2.0, 1000));

    auto series = TimestampChartWidget::buildTimestampSeries(packets, AVMEDIA_TYPE_VIDEO, false);
    EXPECT_EQ(series.size(), 2);
    EXPECT_NEAR(series[0].x(), 1.0, 0.001);  // X = dtsTime
    EXPECT_NEAR(series[1].x(), 2.0, 0.001);
    EXPECT_NEAR(series[0].y(), 1000.0, 0.1); // Y = dtsTime*1000 ms
    EXPECT_NEAR(series[1].y(), 2000.0, 0.1);
}

TEST(TimestampChartWidgetTest, DetectAnomalies_Normal) {
    // 正常递增的 DTS
    QVector<PacketInfo> packets;
    for (int i = 0; i < 10; ++i)
        packets.append(makePacket(i, AVMEDIA_TYPE_VIDEO, i * 0.04, i * 1000));

    auto anomalies = TimestampChartWidget::detectAnomalies(packets, AVMEDIA_TYPE_VIDEO);
    EXPECT_TRUE(anomalies.isEmpty());
}

TEST(TimestampChartWidgetTest, DetectAnomalies_Rollback) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, 0.0,   0));
    packets.append(makePacket(1, AVMEDIA_TYPE_VIDEO, 1.0,   1000));
    packets.append(makePacket(2, AVMEDIA_TYPE_VIDEO, 0.5,   2000)); // 回跳！
    packets.append(makePacket(3, AVMEDIA_TYPE_VIDEO, 1.5,   3000));

    auto anomalies = TimestampChartWidget::detectAnomalies(packets, AVMEDIA_TYPE_VIDEO);
    ASSERT_EQ(anomalies.size(), 1);
    EXPECT_EQ(anomalies[0].packetIndex, 2);
    EXPECT_EQ(anomalies[0].type, "rollback");
}

TEST(TimestampChartWidgetTest, DetectAnomalies_Jump) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, 0.0,   0));
    packets.append(makePacket(1, AVMEDIA_TYPE_VIDEO, 0.04,  1000));
    packets.append(makePacket(2, AVMEDIA_TYPE_VIDEO, 5.0,   2000)); // 跳变! delta = 4960ms > 1000ms

    auto anomalies = TimestampChartWidget::detectAnomalies(packets, AVMEDIA_TYPE_VIDEO, 1000.0);
    ASSERT_EQ(anomalies.size(), 1);
    EXPECT_EQ(anomalies[0].packetIndex, 2);
    EXPECT_EQ(anomalies[0].type, "jump");
}

TEST(TimestampChartWidgetTest, DetectAnomalies_IgnoresOtherType) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, 0.0, 0));
    packets.append(makePacket(1, AVMEDIA_TYPE_AUDIO, 0.5, 1000, 1)); // 音频回跳不影响视频
    packets.append(makePacket(2, AVMEDIA_TYPE_VIDEO, 0.04, 2000));

    auto anomalies = TimestampChartWidget::detectAnomalies(packets, AVMEDIA_TYPE_VIDEO);
    EXPECT_TRUE(anomalies.isEmpty());
}

TEST(TimestampChartWidgetTest, WidgetSetAndClear) {
    QVector<PacketInfo> packets;
    packets.append(makePacket(0, AVMEDIA_TYPE_VIDEO, 0.0, 0));
    packets.append(makePacket(1, AVMEDIA_TYPE_AUDIO, 0.0, 1000, 1));

    TimestampChartWidget widget;
    widget.setPackets(packets); // 不应崩溃
    widget.clear();
}
