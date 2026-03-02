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
        delete decoder;
        delete reader;
    }
    PacketReader *reader = nullptr;
    PacketDecoder *decoder = nullptr;

    // 打开文件并初始化 decoder（在需要文件的测试中调用）
    bool openFile(const QString &path) {
        if (!reader->open(path)) return false;
        if (!reader->readAllPackets()) return false;
        decoder = new PacketDecoder(reader);
        return decoder->open();
    }

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

    ASSERT_TRUE(openFile(path));

    int idx = findFirstPacketOfType(AVMEDIA_TYPE_VIDEO);
    ASSERT_GE(idx, 0);

    // 第一个视频帧应该是关键帧
    EXPECT_TRUE(reader->packetAt(idx).flags & AV_PKT_FLAG_KEY);

    QString err;
    QImage img = decoder->decodeVideoPacket(idx, &err);
    EXPECT_FALSE(img.isNull()) << "Error: " << err.toStdString();
}

TEST_F(PacketDecoderTest, DecodeKeyFrameDimensions) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(openFile(path));

    int idx = findFirstPacketOfType(AVMEDIA_TYPE_VIDEO);
    ASSERT_GE(idx, 0);

    QImage img = decoder->decodeVideoPacket(idx);
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

    ASSERT_TRUE(openFile(path));

    int idx = findFirstNonKeyVideoPacket();
    if (idx < 0) {
        GTEST_SKIP() << "No non-key video packets found";
    }

    QString err;
    QImage img = decoder->decodeVideoPacket(idx, &err);
    EXPECT_FALSE(img.isNull()) << "Error: " << err.toStdString();
}

TEST_F(PacketDecoderTest, DecodeAudioPacket) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(openFile(path));

    int idx = findFirstPacketOfType(AVMEDIA_TYPE_AUDIO);
    ASSERT_GE(idx, 0);

    QString err;
    AudioData data = decoder->decodeAudioPacket(idx, &err);
    EXPECT_FALSE(data.samples.isEmpty()) << "Error: " << err.toStdString();
    EXPECT_GT(data.sampleRate, 0);
    EXPECT_GT(data.channels, 0);
}

TEST_F(PacketDecoderTest, DecodeAudioFormat) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(openFile(path));

    int idx = findFirstPacketOfType(AVMEDIA_TYPE_AUDIO);
    ASSERT_GE(idx, 0);

    AudioData data = decoder->decodeAudioPacket(idx);
    ASSERT_FALSE(data.samples.isEmpty());

    // 采样值应该在合理范围内（float，约 -1.0 ~ 1.0 但可能略超）
    for (int i = 0; i < qMin(data.samples.size(), 1000); ++i) {
        EXPECT_GE(data.samples[i], -2.0f);
        EXPECT_LE(data.samples[i], 2.0f);
    }
}

TEST_F(PacketDecoderTest, DecodeAudioPacketNonSilent) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(openFile(path));

    // 找一个非首个的音频包（避免 priming 帧问题）
    int audioCount = 0;
    int idx = -1;
    for (int i = 0; i < reader->packetCount(); ++i) {
        if (reader->packetAt(i).mediaType == AVMEDIA_TYPE_AUDIO) {
            audioCount++;
            if (audioCount >= 3) { // 第 3 个音频包
                idx = i;
                break;
            }
        }
    }
    if (idx < 0) {
        GTEST_SKIP() << "Not enough audio packets";
    }

    QString err;
    AudioData data = decoder->decodeAudioPacket(idx, &err);
    ASSERT_FALSE(data.samples.isEmpty()) << "Error: " << err.toStdString();

    // 验证不全是静音（最大振幅 > 0.001）
    float maxAbs = 0.0f;
    for (float s : data.samples) {
        float a = std::fabs(s);
        if (a > maxAbs) maxAbs = a;
    }
    EXPECT_GT(maxAbs, 0.001f) << "Audio samples appear to be all silent (max abs value: " << maxAbs << ")";
}

TEST_F(PacketDecoderTest, InvalidPacketIndex) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(openFile(path));

    QString err;
    QImage img = decoder->decodeVideoPacket(-1, &err);
    EXPECT_TRUE(img.isNull());
    EXPECT_FALSE(err.isEmpty());

    img = decoder->decodeVideoPacket(999999, &err);
    EXPECT_TRUE(img.isNull());
}

TEST_F(PacketDecoderTest, VideoStreamOnlyPacket) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(openFile(path));

    // 对音频 Packet 调用 decodeVideoPacket
    int audioIdx = findFirstPacketOfType(AVMEDIA_TYPE_AUDIO);
    if (audioIdx >= 0) {
        QString err;
        QImage img = decoder->decodeVideoPacket(audioIdx, &err);
        EXPECT_TRUE(img.isNull());
    }
}

TEST_F(PacketDecoderTest, NullReader) {
    PacketDecoder nullDecoder(nullptr);
    QString err;
    QImage img = nullDecoder.decodeVideoPacket(0, &err);
    EXPECT_TRUE(img.isNull());
    EXPECT_FALSE(err.isEmpty());

    AudioData ad = nullDecoder.decodeAudioPacket(0, &err);
    EXPECT_TRUE(ad.samples.isEmpty());

    QString st = nullDecoder.decodeSubtitlePacket(0, &err);
    EXPECT_TRUE(st.isEmpty());
}

// 验证同一实例连续解码多个不同 packet（Context 复用正确性）
TEST_F(PacketDecoderTest, ReuseContextMultipleDecodes) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(openFile(path));

    // 找第一个和第二个视频包
    int first = -1, second = -1;
    for (int i = 0; i < reader->packetCount(); ++i) {
        if (reader->packetAt(i).mediaType == AVMEDIA_TYPE_VIDEO) {
            if (first < 0) first = i;
            else if (second < 0) { second = i; break; }
        }
    }
    ASSERT_GE(first, 0);
    ASSERT_GE(second, 0);

    QImage img1 = decoder->decodeVideoPacket(first);
    EXPECT_FALSE(img1.isNull());

    QImage img2 = decoder->decodeVideoPacket(second);
    EXPECT_FALSE(img2.isNull());
}

// --- VVC 首 GOP leading picture 适配测试 ---

// 辅助方法：查找首 GOP leading B 帧（PTS 早于所属 GOP 关键帧的视频帧）
static int findFirstGopLeadingPacket(PacketReader *reader) {
    for (int i = 0; i < reader->packetCount(); ++i) {
        const PacketInfo &pkt = reader->packetAt(i);
        if (pkt.mediaType != AVMEDIA_TYPE_VIDEO) continue;
        int gopKeyIdx = reader->findGopKeyFrame(i);
        if (gopKeyIdx < 0) continue;
        const PacketInfo &gopPkt = reader->packetAt(gopKeyIdx);
        // leading picture: 目标 PTS < 关键帧 PTS，且该关键帧是流的首个关键帧
        if (pkt.pts != AV_NOPTS_VALUE && gopPkt.pts != AV_NOPTS_VALUE &&
            pkt.pts < gopPkt.pts) {
            // 检查是否为首 GOP（无更早的关键帧）
            int prevGop = (gopKeyIdx > 0) ? reader->findGopKeyFrame(gopKeyIdx - 1) : -1;
            if (prevGop < 0 || prevGop == gopKeyIdx) {
                return i; // 这是首 GOP leading picture
            }
        }
    }
    return -1;
}

// 辅助方法：查找带 DISCARD 标志的视频帧
static int findDiscardVideoPacket(PacketReader *reader) {
    for (int i = 0; i < reader->packetCount(); ++i) {
        const PacketInfo &pkt = reader->packetAt(i);
        if (pkt.mediaType == AVMEDIA_TYPE_VIDEO && (pkt.flags & AV_PKT_FLAG_DISCARD))
            return i;
    }
    return -1;
}

// VVC 首 GOP leading B 帧解码：验证解码不崩溃且能产出图像（精确帧或兜底帧）
TEST_F(PacketDecoderTest, VvcFirstGopLeadingDecode) {
    // 优先尝试 VVC 测试文件，不存在则跳过
    QString path = testFile("test_vvc.mp4");
    if (!QFile::exists(path)) {
        path = testFile("test_vvc_aac.mp4");
    }
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "No VVC test file found (test_vvc.mp4 / test_vvc_aac.mp4)";
    }

    ASSERT_TRUE(openFile(path));

    int idx = findFirstGopLeadingPacket(reader);
    if (idx < 0) {
        GTEST_SKIP() << "No first-GOP leading B frame found in VVC test file";
    }

    const PacketInfo &pkt = reader->packetAt(idx);
    EXPECT_EQ(pkt.mediaType, AVMEDIA_TYPE_VIDEO);

    // 解码应产出图像（精确帧或兜底到最近可解码帧）
    QString err;
    QImage img = decoder->decodeVideoPacket(idx, &err);
    // 不强制要求成功（RASL 帧可能真的不可解码），但不应崩溃
    if (img.isNull()) {
        // 如果解码失败，错误信息应包含 "首 GOP" 或 "leading" 相关描述
        EXPECT_TRUE(err.contains("leading") || err.contains("GOP") || err.contains("RASL"))
            << "Unexpected error: " << err.toStdString();
    } else {
        EXPECT_GT(img.width(), 0);
        EXPECT_GT(img.height(), 0);
    }
}

// VVC DISCARD 帧解码：验证 DISCARD 标志的帧也能尝试解码
TEST_F(PacketDecoderTest, VvcDiscardPacketDecode) {
    QString path = testFile("test_vvc.mp4");
    if (!QFile::exists(path)) {
        path = testFile("test_vvc_aac.mp4");
    }
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "No VVC test file found";
    }

    ASSERT_TRUE(openFile(path));

    int idx = findDiscardVideoPacket(reader);
    if (idx < 0) {
        GTEST_SKIP() << "No DISCARD video packets found";
    }

    // 解码不应崩溃
    QString err;
    QImage img = decoder->decodeVideoPacket(idx, &err);
    // 成功或失败都可接受，关键是不崩溃
    (void)img;
}

// 回归测试：H.264 文件（无 leading picture）的解码不应受影响
TEST_F(PacketDecoderTest, H264NoLeadingPictureRegression) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(openFile(path));

    // H.264 一般不会有首 GOP leading picture
    int leadingIdx = findFirstGopLeadingPacket(reader);
    // 即使有 leading picture 也应能解码，没有 leading 更好

    // 验证关键帧解码正常
    int keyIdx = findFirstPacketOfType(AVMEDIA_TYPE_VIDEO);
    ASSERT_GE(keyIdx, 0);
    QImage img1 = decoder->decodeVideoPacket(keyIdx);
    EXPECT_FALSE(img1.isNull());

    // 验证非关键帧解码正常
    int nonKeyIdx = findFirstNonKeyVideoPacket();
    if (nonKeyIdx >= 0) {
        QImage img2 = decoder->decodeVideoPacket(nonKeyIdx);
        EXPECT_FALSE(img2.isNull());
    }
}

// 验证 open / close / 重新 open 的生命周期
TEST_F(PacketDecoderTest, OpenCloseCycle) {
    QString path = testFile("test_h264_aac.mp4");
    if (!QFile::exists(path)) {
        GTEST_SKIP() << "Test file not found";
    }

    ASSERT_TRUE(reader->open(path));
    ASSERT_TRUE(reader->readAllPackets());

    decoder = new PacketDecoder(reader);
    ASSERT_TRUE(decoder->open());
    EXPECT_TRUE(decoder->isOpen());

    int idx = findFirstPacketOfType(AVMEDIA_TYPE_VIDEO);
    ASSERT_GE(idx, 0);

    QImage img1 = decoder->decodeVideoPacket(idx);
    EXPECT_FALSE(img1.isNull());

    decoder->close();
    EXPECT_FALSE(decoder->isOpen());

    // 重新打开后应能继续解码
    ASSERT_TRUE(decoder->open());
    QImage img2 = decoder->decodeVideoPacket(idx);
    EXPECT_FALSE(img2.isNull());
}
