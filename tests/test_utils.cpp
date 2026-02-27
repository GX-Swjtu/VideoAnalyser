#include <gtest/gtest.h>
#include "packetlistmodel.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
}

// ---- 时间格式化测试 ----

TEST(UtilsTest, FormatTimeZero) {
    EXPECT_EQ(PacketListModel::formatTime(0.0), "00:00:00.000");
}

TEST(UtilsTest, FormatTimeNegative) {
    EXPECT_EQ(PacketListModel::formatTime(-1.0), "N/A");
}

TEST(UtilsTest, FormatTimeSeconds) {
    EXPECT_EQ(PacketListModel::formatTime(5.123), "00:00:05.123");
}

TEST(UtilsTest, FormatTimeMinutes) {
    EXPECT_EQ(PacketListModel::formatTime(65.5), "00:01:05.500");
}

TEST(UtilsTest, FormatTimeHours) {
    EXPECT_EQ(PacketListModel::formatTime(3661.0), "01:01:01.000");
}

// ---- 偏移格式化测试 ----

TEST(UtilsTest, FormatOffsetZero) {
    EXPECT_EQ(PacketListModel::formatOffset(0), "0x00000000");
}

TEST(UtilsTest, FormatOffsetPositive) {
    EXPECT_EQ(PacketListModel::formatOffset(0x1A2B3C), "0x001A2B3C");
}

TEST(UtilsTest, FormatOffsetNegative) {
    EXPECT_EQ(PacketListModel::formatOffset(-1), "N/A");
}

TEST(UtilsTest, FormatOffsetLarge) {
    EXPECT_EQ(PacketListModel::formatOffset(0xFFFFFFFF), "0xFFFFFFFF");
}

// ---- 标志位格式化测试 ----

TEST(UtilsTest, FormatFlagsNone) {
    EXPECT_EQ(PacketListModel::formatFlags(0), "");
}

TEST(UtilsTest, FormatFlagsKey) {
    EXPECT_EQ(PacketListModel::formatFlags(AV_PKT_FLAG_KEY), "KEY");
}

TEST(UtilsTest, FormatFlagsMultiple) {
    int flags = AV_PKT_FLAG_KEY | AV_PKT_FLAG_CORRUPT;
    QString result = PacketListModel::formatFlags(flags);
    EXPECT_TRUE(result.contains("KEY"));
    EXPECT_TRUE(result.contains("CORRUPT"));
}

// ---- 媒体类型字符串测试 ----

TEST(UtilsTest, MediaTypeStringVideo) {
    EXPECT_EQ(PacketListModel::mediaTypeString(AVMEDIA_TYPE_VIDEO), "video");
}

TEST(UtilsTest, MediaTypeStringAudio) {
    EXPECT_EQ(PacketListModel::mediaTypeString(AVMEDIA_TYPE_AUDIO), "audio");
}

TEST(UtilsTest, MediaTypeStringSubtitle) {
    EXPECT_EQ(PacketListModel::mediaTypeString(AVMEDIA_TYPE_SUBTITLE), "subtitle");
}

TEST(UtilsTest, MediaTypeStringUnknown) {
    EXPECT_EQ(PacketListModel::mediaTypeString(AVMEDIA_TYPE_UNKNOWN), "unknown");
}

// ---- 媒体类型图标测试 ----

TEST(UtilsTest, MediaTypeIconVideo) {
    EXPECT_FALSE(PacketListModel::mediaTypeIcon(AVMEDIA_TYPE_VIDEO).isEmpty());
}

TEST(UtilsTest, MediaTypeIconAudio) {
    EXPECT_FALSE(PacketListModel::mediaTypeIcon(AVMEDIA_TYPE_AUDIO).isEmpty());
}
