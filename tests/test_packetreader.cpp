#include <gtest/gtest.h>
#include "packetreader.h"

#include <QCoreApplication>
#include <QFile>

static const char *testDataDir() {
    return TEST_DATA_DIR;
}

static QString testFile(const char *name) {
    return QString::fromUtf8(testDataDir()) + "/" + name;
}

class PacketReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        reader = new PacketReader();
    }
    void TearDown() override {
        delete reader;
    }
    PacketReader *reader = nullptr;
};

TEST_F(PacketReaderTest, OpenValidFile) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found: " << path.toStdString();
    }
    EXPECT_TRUE(reader->open(path));
    EXPECT_TRUE(reader->isOpen());
}

TEST_F(PacketReaderTest, OpenInvalidFile) {
    EXPECT_FALSE(reader->open("/nonexistent/file.mp4"));
    EXPECT_FALSE(reader->isOpen());
}

TEST_F(PacketReaderTest, OpenEmptyPath) {
    EXPECT_FALSE(reader->open(""));
}

TEST_F(PacketReaderTest, ReadAllPackets) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());
    EXPECT_GT(reader->packetCount(), 0);
}

TEST_F(PacketReaderTest, PacketMetadataFields) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());
    ASSERT_GT(reader->packetCount(), 0);

    for (int i = 0; i < qMin(reader->packetCount(), 100); ++i) {
        const PacketInfo &pkt = reader->packetAt(i);
        EXPECT_EQ(pkt.index, i);
        EXPECT_GE(pkt.streamIndex, 0);
        EXPECT_LT(pkt.streamIndex, static_cast<int>(reader->streams().size()));
        EXPECT_GT(pkt.size, 0);
    }
}

TEST_F(PacketReaderTest, StreamInfoCorrect) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));

    const auto &streams = reader->streams();
    EXPECT_GE(streams.size(), 1); // 至少有一个流

    bool hasVideo = false, hasAudio = false;
    for (const auto &s : streams) {
        if (s.mediaType == AVMEDIA_TYPE_VIDEO) {
            hasVideo = true;
            EXPECT_FALSE(s.codecName.isEmpty());
            EXPECT_NE(s.codecpar, nullptr);
        }
        if (s.mediaType == AVMEDIA_TYPE_AUDIO) {
            hasAudio = true;
            EXPECT_GT(s.sampleRate, 0);
            EXPECT_GT(s.channels, 0);
        }
    }
    EXPECT_TRUE(hasVideo);
    EXPECT_TRUE(hasAudio);
}

TEST_F(PacketReaderTest, KeyFrameIndexTable) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    // 查找第一个视频 Packet
    for (int i = 0; i < reader->packetCount(); ++i) {
        const PacketInfo &pkt = reader->packetAt(i);
        if (pkt.mediaType == AVMEDIA_TYPE_VIDEO) {
            // 第一个视频 Packet 应该是关键帧
            EXPECT_TRUE(pkt.flags & AV_PKT_FLAG_KEY);
            EXPECT_GE(pkt.gopKeyFrameIndex, 0);
            break;
        }
    }
}

TEST_F(PacketReaderTest, FindGopKeyFrame) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    // 对每个视频 Packet，验证 gopKeyFrame 正确
    for (int i = 0; i < qMin(reader->packetCount(), 200); ++i) {
        const PacketInfo &pkt = reader->packetAt(i);
        if (pkt.mediaType == AVMEDIA_TYPE_VIDEO) {
            int kfIdx = reader->findGopKeyFrame(i);
            EXPECT_GE(kfIdx, 0);
            EXPECT_LE(kfIdx, i);
            // 该关键帧 packet 必须有 KEY 标志
            const PacketInfo &kf = reader->packetAt(kfIdx);
            EXPECT_TRUE(kf.flags & AV_PKT_FLAG_KEY);
        }
    }
}

TEST_F(PacketReaderTest, ReadPacketData) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());
    ASSERT_GT(reader->packetCount(), 0);

    // 读取第一个 Packet 的原始数据
    const PacketInfo &pkt = reader->packetAt(0);
    QByteArray data = reader->readPacketData(0);
    EXPECT_EQ(data.size(), pkt.size);
}

TEST_F(PacketReaderTest, TimeConversion) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    double duration = reader->durationSeconds();
    for (int i = 0; i < qMin(reader->packetCount(), 100); ++i) {
        const PacketInfo &pkt = reader->packetAt(i);
        if (pkt.ptsTime >= 0) {
            EXPECT_GE(pkt.ptsTime, 0.0);
            EXPECT_LE(pkt.ptsTime, duration + 1.0);
        }
    }
}

TEST_F(PacketReaderTest, CloseAndReopen) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());
    int count1 = reader->packetCount();

    reader->close();
    EXPECT_FALSE(reader->isOpen());
    EXPECT_EQ(reader->packetCount(), 0);

    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());
    EXPECT_EQ(reader->packetCount(), count1);
}

TEST_F(PacketReaderTest, FormatName) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    EXPECT_FALSE(reader->formatName().isEmpty());
}

TEST_F(PacketReaderTest, FindGopKeyFrameForNonVideo) {
    // For non-video/invalid, expect -1
    EXPECT_EQ(reader->findGopKeyFrame(-1), -1);
    EXPECT_EQ(reader->findGopKeyFrame(99999), -1);
}
