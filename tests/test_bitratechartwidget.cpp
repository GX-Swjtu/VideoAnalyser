#include <gtest/gtest.h>
#include "bitratechartwidget.h"
#include "packetreader.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
}

static PacketInfo makeVideoPacket(int index, double dtsTime, int size,
                                   double durationTime, int64_t pos, int stream = 0) {
    PacketInfo p{};
    p.index = index;
    p.streamIndex = stream;
    p.mediaType = AVMEDIA_TYPE_VIDEO;
    p.dts = static_cast<int64_t>(dtsTime * 1000);
    p.dtsTime = dtsTime;
    p.pts = p.dts;
    p.ptsTime = dtsTime;
    p.pos = pos;
    p.size = size;
    p.flags = 0;
    p.duration = static_cast<int64_t>(durationTime * 1000);
    p.durationTime = durationTime;
    p.gopKeyFrameIndex = -1;
    return p;
}

TEST(BitrateChartWidgetTest, BuildBitrateSeries_Empty) {
    QVector<PacketInfo> packets;
    auto series = BitrateChartWidget::buildBitrateSeries(packets, 0, true);
    EXPECT_TRUE(series.isEmpty());
}

TEST(BitrateChartWidgetTest, BuildBitrateSeries_Normal) {
    QVector<PacketInfo> packets;
    // size=10000 bytes, duration=0.04s → bitrate = 10000*8/0.04/1e6 = 2.0 Mbps
    packets.append(makeVideoPacket(0, 0.0, 10000, 0.04, 0));
    packets.append(makeVideoPacket(1, 0.04, 10000, 0.04, 10000));

    auto series = BitrateChartWidget::buildBitrateSeries(packets, 0, true);
    ASSERT_EQ(series.size(), 2);
    EXPECT_NEAR(series[0].y(), 2.0, 0.01);
    EXPECT_NEAR(series[1].y(), 2.0, 0.01);
}

TEST(BitrateChartWidgetTest, BuildBitrateSeries_ZeroDuration) {
    QVector<PacketInfo> packets;
    packets.append(makeVideoPacket(0, 0.0, 10000, 0.0, 0)); // duration=0 → bitrate=0
    auto series = BitrateChartWidget::buildBitrateSeries(packets, 0, true);
    ASSERT_EQ(series.size(), 1);
    EXPECT_NEAR(series[0].y(), 0.0, 0.01);
}

TEST(BitrateChartWidgetTest, BuildBitrateSeries_TimeMode) {
    QVector<PacketInfo> packets;
    packets.append(makeVideoPacket(0, 1.0, 10000, 0.04, 0));
    auto series = BitrateChartWidget::buildBitrateSeries(packets, 0, false);
    ASSERT_EQ(series.size(), 1);
    EXPECT_NEAR(series[0].x(), 1.0, 0.001); // X = dtsTime
}

TEST(BitrateChartWidgetTest, BuildBitrateSeries_FiltersStream) {
    QVector<PacketInfo> packets;
    packets.append(makeVideoPacket(0, 0.0, 10000, 0.04, 0, 0));    // 视频流 0
    PacketInfo audio{};
    audio.index = 1;
    audio.streamIndex = 1;
    audio.mediaType = AVMEDIA_TYPE_AUDIO;
    audio.dtsTime = 0.0;
    audio.pos = 10000;
    audio.size = 500;
    audio.durationTime = 0.023;
    packets.append(audio);

    auto series = BitrateChartWidget::buildBitrateSeries(packets, 0, true);
    EXPECT_EQ(series.size(), 1); // 仅视频 packet
}

TEST(BitrateChartWidgetTest, ComputeAverageBitrate_Normal) {
    QVector<PacketInfo> packets;
    // 2 packets: 各 10000 bytes, 0.04s → total 20000 bytes, 0.08s → avg = 2.0 Mbps
    packets.append(makeVideoPacket(0, 0.0, 10000, 0.04, 0));
    packets.append(makeVideoPacket(1, 0.04, 10000, 0.04, 10000));

    double avg = BitrateChartWidget::computeAverageBitrate(packets, 0);
    EXPECT_NEAR(avg, 2.0, 0.01);
}

TEST(BitrateChartWidgetTest, ComputeAverageBitrate_Empty) {
    QVector<PacketInfo> packets;
    EXPECT_NEAR(BitrateChartWidget::computeAverageBitrate(packets, 0), 0.0, 0.01);
}

TEST(BitrateChartWidgetTest, WidgetSetAndClear) {
    QVector<PacketInfo> packets;
    packets.append(makeVideoPacket(0, 0.0, 10000, 0.04, 0));

    QVector<StreamInfo> streams;
    StreamInfo vs{};
    vs.index = 0;
    vs.mediaType = AVMEDIA_TYPE_VIDEO;
    streams.append(vs);

    BitrateChartWidget widget;
    widget.setPackets(packets, streams);
    widget.clear();
}
