#include <gtest/gtest.h>
#include "packetdecoder.h"
#include "packetreader.h"

#include <QCoreApplication>
#include <QFile>
#include <QImage>

static QString testFile(const char *name) {
    return QString::fromUtf8(TEST_DATA_DIR) + "/" + name;
}

class PacketDecoderTest : public ::testing::Test {
protected:
    void SetUp() override {
        reader = new PacketReader();
    }
    void TearDown() override {
        delete reader;
    }
    PacketReader *reader = nullptr;

    int findFirstPacketOfType(AVMediaType type) {
        for (int i = 0; i < reader->packetCount(); ++i) {
            if (reader->packetAt(i).mediaType == type)
                return i;
        }
        return -1;
    }

    int findFirstNonKeyVideoPacket() {
        for (int i = 0; i < reader->packetCount(); ++i) {
            const PacketInfo &pkt = reader->packetAt(i);
            if (pkt.mediaType == AVMEDIA_TYPE_VIDEO && !(pkt.flags & AV_PKT_FLAG_KEY))
                return i;
        }
        return -1;
    }
};

TEST_F(PacketDecoderTest, DecodeKeyFrame) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    int idx = findFirstPacketOfType(AVMEDIA_TYPE_VIDEO);
    ASSERT_GE(idx, 0);

    // 第一个视频帧应该是关键帧
    EXPECT_TRUE(reader->packetAt(idx).flags & AV_PKT_FLAG_KEY);

    QString err;
    QImage img = PacketDecoder::decodeVideoPacket(reader, idx, &err);
    EXPECT_FALSE(img.isNull()) << "Error: " << err.toStdString();
}

TEST_F(PacketDecoderTest, DecodeKeyFrameDimensions) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    int idx = findFirstPacketOfType(AVMEDIA_TYPE_VIDEO);
    ASSERT_GE(idx, 0);

    QImage img = PacketDecoder::decodeVideoPacket(reader, idx);
    ASSERT_FALSE(img.isNull());

    // 测试视频是 320x240
    EXPECT_EQ(img.width(), 320);
    EXPECT_EQ(img.height(), 240);
}

TEST_F(PacketDecoderTest, DecodePBFrame) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    int idx = findFirstNonKeyVideoPacket();
    if (idx < 0) {
        GTEST_SKIP() << "No non-key video packets found";
    }

    QString err;
    QImage img = PacketDecoder::decodeVideoPacket(reader, idx, &err);
    EXPECT_FALSE(img.isNull()) << "Error: " << err.toStdString();
}

TEST_F(PacketDecoderTest, DecodeAudioPacket) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    int idx = findFirstPacketOfType(AVMEDIA_TYPE_AUDIO);
    ASSERT_GE(idx, 0);

    QString err;
    AudioData data = PacketDecoder::decodeAudioPacket(reader, idx, &err);
    EXPECT_FALSE(data.samples.isEmpty()) << "Error: " << err.toStdString();
    EXPECT_GT(data.sampleRate, 0);
    EXPECT_GT(data.channels, 0);
}

TEST_F(PacketDecoderTest, DecodeAudioFormat) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    int idx = findFirstPacketOfType(AVMEDIA_TYPE_AUDIO);
    ASSERT_GE(idx, 0);

    AudioData data = PacketDecoder::decodeAudioPacket(reader, idx);
    ASSERT_FALSE(data.samples.isEmpty());

    // 采样值应该在合理范围内（float，约 -1.0 ~ 1.0 但可能略超）
    for (int i = 0; i < qMin(data.samples.size(), 1000); ++i) {
        EXPECT_GE(data.samples[i], -2.0f);
        EXPECT_LE(data.samples[i], 2.0f);
    }
}

TEST_F(PacketDecoderTest, InvalidPacketIndex) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    QString err;
    QImage img = PacketDecoder::decodeVideoPacket(reader, -1, &err);
    EXPECT_TRUE(img.isNull());
    EXPECT_FALSE(err.isEmpty());

    img = PacketDecoder::decodeVideoPacket(reader, 999999, &err);
    EXPECT_TRUE(img.isNull());
}

TEST_F(PacketDecoderTest, VideoStreamOnlyPacket) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    // 对音频 Packet 调用 decodeVideoPacket
    int audioIdx = findFirstPacketOfType(AVMEDIA_TYPE_AUDIO);
    if (audioIdx >= 0) {
        QString err;
        QImage img = PacketDecoder::decodeVideoPacket(reader, audioIdx, &err);
        EXPECT_TRUE(img.isNull());
    }
}

TEST_F(PacketDecoderTest, NullReader) {
    QString err;
    QImage img = PacketDecoder::decodeVideoPacket(nullptr, 0, &err);
    EXPECT_TRUE(img.isNull());
    EXPECT_FALSE(err.isEmpty());

    AudioData ad = PacketDecoder::decodeAudioPacket(nullptr, 0, &err);
    EXPECT_TRUE(ad.samples.isEmpty());

    QString st = PacketDecoder::decodeSubtitlePacket(nullptr, 0, &err);
    EXPECT_TRUE(st.isEmpty());
}
