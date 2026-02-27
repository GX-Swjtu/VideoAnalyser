#include <gtest/gtest.h>
#include "audiospectrogramwidget.h"

#include <cmath>

// --- MixToMono ---

TEST(AudioSpectrogramWidgetTest, MixToMono_Stereo) {
    // 双声道：L=1.0, R=0.0 → mono=0.5
    QVector<float> interleaved = {1.0f, 0.0f, 0.5f, -0.5f, 0.0f, 1.0f};
    auto mono = AudioSpectrogramWidget::mixToMono(interleaved, 2);
    ASSERT_EQ(mono.size(), 3);
    EXPECT_FLOAT_EQ(mono[0], 0.5f);
    EXPECT_FLOAT_EQ(mono[1], 0.0f);
    EXPECT_FLOAT_EQ(mono[2], 0.5f);
}

TEST(AudioSpectrogramWidgetTest, MixToMono_SingleChannel) {
    QVector<float> data = {0.1f, 0.2f, 0.3f};
    auto mono = AudioSpectrogramWidget::mixToMono(data, 1);
    ASSERT_EQ(mono.size(), 3);
    EXPECT_FLOAT_EQ(mono[0], 0.1f);
    EXPECT_FLOAT_EQ(mono[1], 0.2f);
    EXPECT_FLOAT_EQ(mono[2], 0.3f);
}

TEST(AudioSpectrogramWidgetTest, MixToMono_Empty) {
    auto mono = AudioSpectrogramWidget::mixToMono({}, 2);
    EXPECT_TRUE(mono.isEmpty());
}

// --- PeakDb ---

TEST(AudioSpectrogramWidgetTest, ComputePeakDb_Silence) {
    QVector<float> silence(1024, 0.0f);
    float peak = AudioSpectrogramWidget::computePeakDb(silence);
    EXPECT_LT(peak, -80.0f);
}

TEST(AudioSpectrogramWidgetTest, ComputePeakDb_FullScale) {
    QVector<float> data = {0.0f, 1.0f, -0.5f};
    float peak = AudioSpectrogramWidget::computePeakDb(data);
    EXPECT_NEAR(peak, 0.0f, 0.01f);
}

TEST(AudioSpectrogramWidgetTest, ComputePeakDb_HalfScale) {
    QVector<float> data = {0.0f, 0.5f, -0.5f};
    float peak = AudioSpectrogramWidget::computePeakDb(data);
    // 20*log10(0.5) ≈ -6.02
    EXPECT_NEAR(peak, -6.02f, 0.1f);
}

TEST(AudioSpectrogramWidgetTest, ComputePeakDb_Empty) {
    float peak = AudioSpectrogramWidget::computePeakDb({});
    EXPECT_LT(peak, -90.0f);
}

// --- RmsDb ---

TEST(AudioSpectrogramWidgetTest, ComputeRmsDb_SineWave) {
    // 振幅 1.0 正弦波一个完整周期的 RMS = 1/sqrt(2) ≈ 0.707
    // 20*log10(0.707) ≈ -3.01 dB
    int N = 44100; // 一秒的采样（多个完整周期）
    QVector<float> sine(N);
    for (int i = 0; i < N; ++i) {
        sine[i] = std::sin(2.0 * M_PI * 440.0 * i / 44100.0);
    }
    float rms = AudioSpectrogramWidget::computeRmsDb(sine);
    EXPECT_NEAR(rms, -3.01f, 0.1f);
}

TEST(AudioSpectrogramWidgetTest, ComputeRmsDb_Silence) {
    QVector<float> silence(1024, 0.0f);
    float rms = AudioSpectrogramWidget::computeRmsDb(silence);
    EXPECT_LT(rms, -80.0f);
}

// --- HanningWindow ---

TEST(AudioSpectrogramWidgetTest, HanningWindow_Endpoints) {
    auto win = AudioSpectrogramWidget::generateHanningWindow(256);
    ASSERT_EQ(win.size(), 256);
    // Hanning 窗两端应接近 0
    EXPECT_NEAR(win[0], 0.0f, 0.01f);
    // 中间应接近 1
    EXPECT_NEAR(win[128], 1.0f, 0.01f);
}

// --- Spectrogram ---

TEST(AudioSpectrogramWidgetTest, ComputeSpectrogram_SineWave) {
    // 生成 1024 采样的 440Hz 正弦波（采样率 44100）
    int N = 1024;
    QVector<float> sine(N);
    for (int i = 0; i < N; ++i) {
        sine[i] = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * i / 44100.0f);
    }

    int fftSize = 256;
    int hopSize = 128;
    auto spec = AudioSpectrogramWidget::computeSpectrogram(sine.constData(), N, fftSize, hopSize);

    ASSERT_FALSE(spec.isEmpty());

    int numBins = fftSize / 2 + 1;
    // 每帧应有 numBins 个频率 bin
    EXPECT_EQ(spec[0].size(), numBins);

    // 440Hz 在 fftSize=256, sampleRate=44100 时对应的 bin:
    // bin = 440 * 256 / 44100 ≈ 2.55 → bin 2 或 3
    // 验证 bin 2 或 3 的能量较高
    float maxDb = -100.0f;
    int maxBin = 0;
    // 对第一帧取最高能量 bin（跳过 DC bin 0）
    for (int i = 1; i < spec[0].size(); ++i) {
        if (spec[0][i] > maxDb) {
            maxDb = spec[0][i];
            maxBin = i;
        }
    }
    // 期望最高能量 bin 在 2-4 范围内
    EXPECT_GE(maxBin, 1);
    EXPECT_LE(maxBin, 5);
    // 最高能量应该明显高于静音
    EXPECT_GT(maxDb, -40.0f);
}

TEST(AudioSpectrogramWidgetTest, ComputeSpectrogram_Silence) {
    QVector<float> silence(1024, 0.0f);
    auto spec = AudioSpectrogramWidget::computeSpectrogram(silence.constData(), 1024, 256, 128);
    ASSERT_FALSE(spec.isEmpty());

    // 所有 bin 应该低于 -80dB
    for (const auto &frame : spec) {
        for (float db : frame) {
            EXPECT_LT(db, -80.0f);
        }
    }
}

TEST(AudioSpectrogramWidgetTest, ComputeSpectrogram_Empty) {
    auto spec = AudioSpectrogramWidget::computeSpectrogram(nullptr, 0, 256, 128);
    EXPECT_TRUE(spec.isEmpty());
}

TEST(AudioSpectrogramWidgetTest, ComputeSpectrogram_TooShort) {
    // 少于 fftSize 个采样时，应返回空
    QVector<float> data(100, 0.5f);
    auto spec = AudioSpectrogramWidget::computeSpectrogram(data.constData(), 100, 256, 128);
    EXPECT_TRUE(spec.isEmpty());
}

// --- DbToColor ---

TEST(AudioSpectrogramWidgetTest, DbToColor_Silence) {
    QRgb color = AudioSpectrogramWidget::dbToColor(-90.0f);
    // 应该接近黑色
    EXPECT_LE(qRed(color), 10);
    EXPECT_LE(qGreen(color), 10);
    EXPECT_LE(qBlue(color), 10);
}

TEST(AudioSpectrogramWidgetTest, DbToColor_FullScale) {
    QRgb color = AudioSpectrogramWidget::dbToColor(0.0f);
    // 应该接近白色
    EXPECT_GE(qRed(color), 240);
    EXPECT_GE(qGreen(color), 240);
    EXPECT_GE(qBlue(color), 240);
}

// --- SetAudioData ---

TEST(AudioSpectrogramWidgetTest, SetAudioData) {
    AudioSpectrogramWidget widget;
    AudioData data;
    data.sampleRate = 44100;
    data.channels = 1;
    data.samples.resize(1024);
    for (int i = 0; i < 1024; ++i) {
        data.samples[i] = std::sin(2.0 * M_PI * 440.0 * i / 44100.0);
    }

    widget.setAudioData(data);
    EXPECT_EQ(widget.audioData().sampleRate, 44100);
    EXPECT_EQ(widget.audioData().channels, 1);
    EXPECT_EQ(widget.audioData().samples.size(), 1024);
}

TEST(AudioSpectrogramWidgetTest, SetAudioData_Empty) {
    AudioSpectrogramWidget widget;
    AudioData data;
    widget.setAudioData(data);
    EXPECT_TRUE(widget.audioData().samples.isEmpty());
}
