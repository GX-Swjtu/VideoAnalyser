#include <gtest/gtest.h>
#include "audiowaveformwidget.h"

#include <cmath>

TEST(AudioWaveformWidgetTest, SetAudioData) {
    AudioWaveformWidget widget;
    AudioData data;
    data.sampleRate = 44100;
    data.channels = 2;
    data.samples.resize(1024);
    for (int i = 0; i < 1024; ++i) {
        data.samples[i] = std::sin(2.0 * M_PI * 440.0 * i / 44100.0);
    }

    widget.setAudioData(data);

    EXPECT_EQ(widget.audioData().sampleRate, 44100);
    EXPECT_EQ(widget.audioData().channels, 2);
    EXPECT_EQ(widget.audioData().samples.size(), 1024);
}

TEST(AudioWaveformWidgetTest, EmptyData) {
    AudioWaveformWidget widget;
    AudioData data;
    widget.setAudioData(data);

    EXPECT_TRUE(widget.audioData().samples.isEmpty());
    EXPECT_EQ(widget.audioData().sampleRate, 0);
    EXPECT_EQ(widget.audioData().channels, 0);
}

TEST(AudioWaveformWidgetTest, DownsampleLogic) {
    // 1000 个采样在 100 像素宽度下降采样
    QVector<float> samples(1000);
    for (int i = 0; i < 1000; ++i) {
        samples[i] = std::sin(2.0 * M_PI * i / 100.0);
    }

    auto pairs = AudioWaveformWidget::downsample(samples.constData(), samples.size(), 100);

    EXPECT_EQ(pairs.size(), 100);

    // 每个段包含 10 个采样，min 应该 <= max
    for (const auto &p : pairs) {
        EXPECT_LE(p.minVal, p.maxVal);
        EXPECT_GE(p.minVal, -1.0f);
        EXPECT_LE(p.maxVal, 1.0f);
    }
}

TEST(AudioWaveformWidgetTest, DownsampleNoReduction) {
    // 少量采样不需要降采样
    QVector<float> samples = {0.0f, 0.5f, 1.0f, -0.5f, -1.0f};
    auto pairs = AudioWaveformWidget::downsample(samples.constData(), samples.size(), 100);

    // 不降采样时，每个采样对应一个 min==max 的对
    EXPECT_EQ(pairs.size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_FLOAT_EQ(pairs[i].minVal, samples[i]);
        EXPECT_FLOAT_EQ(pairs[i].maxVal, samples[i]);
    }
}

TEST(AudioWaveformWidgetTest, DownsampleEmpty) {
    auto pairs = AudioWaveformWidget::downsample(nullptr, 0, 100);
    EXPECT_TRUE(pairs.isEmpty());
}

TEST(AudioWaveformWidgetTest, MultiChannel) {
    AudioWaveformWidget widget;
    AudioData data;
    data.sampleRate = 48000;
    data.channels = 6; // 5.1 surround
    data.samples.resize(48000 * 6);
    for (int i = 0; i < data.samples.size(); ++i) {
        data.samples[i] = 0.0f;
    }

    widget.setAudioData(data);
    EXPECT_EQ(widget.audioData().channels, 6);
    EXPECT_EQ(widget.audioData().samples.size(), 48000 * 6);
}
