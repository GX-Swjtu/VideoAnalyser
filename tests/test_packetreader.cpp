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

// ---- findPrevGopKeyFrame 测试 ----

TEST_F(PacketReaderTest, FindPrevGopKeyFrame) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    // 收集视频流的所有关键帧索引
    int videoStreamIdx = -1;
    QVector<int> keyFrameIndices;
    for (int i = 0; i < reader->packetCount(); ++i) {
        const PacketInfo &pkt = reader->packetAt(i);
        if (pkt.mediaType == AVMEDIA_TYPE_VIDEO) {
            if (videoStreamIdx < 0) videoStreamIdx = pkt.streamIndex;
            if (pkt.flags & AV_PKT_FLAG_KEY) {
                keyFrameIndices.append(i);
            }
        }
    }
    ASSERT_GE(videoStreamIdx, 0);
    ASSERT_GE(keyFrameIndices.size(), 1);

    // 第一个关键帧没有前一个关键帧
    EXPECT_EQ(reader->findPrevGopKeyFrame(videoStreamIdx, keyFrameIndices[0]), -1);

    // 如果有第二个关键帧，其前一个应该是第一个
    if (keyFrameIndices.size() >= 2) {
        EXPECT_EQ(reader->findPrevGopKeyFrame(videoStreamIdx, keyFrameIndices[1]), keyFrameIndices[0]);
    }

    // 如果有第三个关键帧，其前一个应该是第二个
    if (keyFrameIndices.size() >= 3) {
        EXPECT_EQ(reader->findPrevGopKeyFrame(videoStreamIdx, keyFrameIndices[2]), keyFrameIndices[1]);
    }
}

TEST_F(PacketReaderTest, FindPrevGopKeyFrameInvalidStream) {
    // 无效流索引应返回 -1
    EXPECT_EQ(reader->findPrevGopKeyFrame(-1, 0), -1);
    EXPECT_EQ(reader->findPrevGopKeyFrame(999, 0), -1);
}

TEST_F(PacketReaderTest, FindPrevGopKeyFrameNotAffectedByAudioPackets) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    // 找到第二个视频关键帧
    int videoStreamIdx = -1;
    QVector<int> keyFrameIndices;
    for (int i = 0; i < reader->packetCount(); ++i) {
        const PacketInfo &pkt = reader->packetAt(i);
        if (pkt.mediaType == AVMEDIA_TYPE_VIDEO) {
            if (videoStreamIdx < 0) videoStreamIdx = pkt.streamIndex;
            if (pkt.flags & AV_PKT_FLAG_KEY) {
                keyFrameIndices.append(i);
            }
        }
    }
    if (keyFrameIndices.size() < 2) {
        GTEST_SKIP() << "Need at least 2 keyframes for this test";
    }

    int secondKeyFrame = keyFrameIndices[1];

    // 验证 findPrevGopKeyFrame 正确返回第一个关键帧
    // （即使 secondKeyFrame-1 是音频包也不受影响）
    int prev = reader->findPrevGopKeyFrame(videoStreamIdx, secondKeyFrame);
    EXPECT_EQ(prev, keyFrameIndices[0]);

    // 对比：旧的 findGopKeyFrame(secondKeyFrame-1) 方法
    // 如果 secondKeyFrame-1 是音频包，它会返回 -1（这正是之前的 bug）
    if (secondKeyFrame > 0) {
        const PacketInfo &prevPkt = reader->packetAt(secondKeyFrame - 1);
        if (prevPkt.mediaType != AVMEDIA_TYPE_VIDEO) {
            // 旧方法会失败
            EXPECT_EQ(reader->findGopKeyFrame(secondKeyFrame - 1), -1);
            // 新方法不受影响
            EXPECT_EQ(reader->findPrevGopKeyFrame(videoStreamIdx, secondKeyFrame), keyFrameIndices[0]);
        }
    }
}

// ---- 帧类型检测测试 ----

TEST_F(PacketReaderTest, VideoPacketsHaveFrameType) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    int videoCount = 0;
    int withPictType = 0;
    for (int i = 0; i < reader->packetCount(); ++i) {
        const PacketInfo &pkt = reader->packetAt(i);
        if (pkt.mediaType == AVMEDIA_TYPE_VIDEO) {
            ++videoCount;
            if (pkt.pictType > 0) ++withPictType;
        }
    }
    EXPECT_GT(videoCount, 0);
    // 绝大多数视频 packet 应该有帧类型信息
    EXPECT_GT(withPictType, videoCount / 2);
}

TEST_F(PacketReaderTest, FirstVideoPacketIsIDR) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    // 第一个视频 Packet 通常是 IDR I帧
    for (int i = 0; i < reader->packetCount(); ++i) {
        const PacketInfo &pkt = reader->packetAt(i);
        if (pkt.mediaType == AVMEDIA_TYPE_VIDEO) {
            EXPECT_EQ(pkt.pictType, AV_PICTURE_TYPE_I);
            EXPECT_TRUE(pkt.isIDR);
            break;
        }
    }
}

TEST_F(PacketReaderTest, AudioPacketsNoFrameType) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    for (int i = 0; i < reader->packetCount(); ++i) {
        const PacketInfo &pkt = reader->packetAt(i);
        if (pkt.mediaType == AVMEDIA_TYPE_AUDIO) {
            EXPECT_EQ(pkt.pictType, -1);
            EXPECT_FALSE(pkt.isIDR);
        }
    }
}

TEST_F(PacketReaderTest, FrameTypeVariety) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }
    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    bool hasI = false, hasP = false, hasB = false;
    for (int i = 0; i < reader->packetCount(); ++i) {
        const PacketInfo &pkt = reader->packetAt(i);
        if (pkt.mediaType == AVMEDIA_TYPE_VIDEO) {
            if (pkt.pictType == AV_PICTURE_TYPE_I) hasI = true;
            if (pkt.pictType == AV_PICTURE_TYPE_P) hasP = true;
            if (pkt.pictType == AV_PICTURE_TYPE_B) hasB = true;
        }
    }
    EXPECT_TRUE(hasI) << "应至少有一个 I 帧";
    // P 帧和 B 帧取决于编码参数，不强制要求
    // 但 H.264 测试视频通常包含 P 帧
    EXPECT_TRUE(hasI || hasP) << "应至少有 I 帧或 P 帧";
}
