#include "audiowaveformwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>

AudioWaveformWidget::AudioWaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(100);
    setMinimumWidth(200);
}

void AudioWaveformWidget::setAudioData(const AudioData &data)
{
    m_data = data;
    update();
}

QVector<AudioWaveformWidget::MinMaxPair> AudioWaveformWidget::downsample(
    const float *samples, int count, int targetWidth)
{
    QVector<MinMaxPair> result;
    if (count <= 0 || targetWidth <= 0) return result;

    if (count <= targetWidth * 2) {
        // 不需要降采样
        result.reserve(count);
        for (int i = 0; i < count; ++i) {
            result.append({samples[i], samples[i]});
        }
        return result;
    }

    result.reserve(targetWidth);
    double step = static_cast<double>(count) / targetWidth;

    for (int i = 0; i < targetWidth; ++i) {
        int start = static_cast<int>(i * step);
        int end = qMin(static_cast<int>((i + 1) * step), count);
        if (start >= end) {
            result.append({0.0f, 0.0f});
            continue;
        }
        auto [minIt, maxIt] = std::minmax_element(samples + start, samples + end);
        result.append({*minIt, *maxIt});
    }

    return result;
}

void AudioWaveformWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 背景
    painter.fillRect(rect(), QColor(30, 30, 30));

    if (m_data.samples.isEmpty() || m_data.channels <= 0) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No audio data"));
        return;
    }

    int channels = m_data.channels;
    int samplesPerChannel = m_data.samples.size() / channels;
    int w = width();
    int h = height();
    int channelHeight = h / channels;

    // 信息标注
    painter.setPen(QColor(180, 180, 180));
    double duration = (samplesPerChannel > 0 && m_data.sampleRate > 0)
                          ? static_cast<double>(samplesPerChannel) / m_data.sampleRate
                          : 0.0;
    QString info = QStringLiteral("采样率: %1Hz | 声道: %2 | 采样数: %3 | 时长: %4s")
                       .arg(m_data.sampleRate)
                       .arg(channels)
                       .arg(samplesPerChannel)
                       .arg(duration, 0, 'f', 3);
    painter.drawText(8, 14, info);

    // 每个声道的颜色
    static const QColor channelColors[] = {
        QColor(0, 255, 0),    // 绿色
        QColor(0, 200, 255),  // 青色
        QColor(255, 200, 0),  // 黄色
        QColor(255, 100, 100) // 红色
    };

    for (int ch = 0; ch < channels; ++ch) {
        int yBase = ch * channelHeight;
        int centerY = yBase + channelHeight / 2;
        float halfH = channelHeight / 2.0f * 0.8f; // 留一点边距

        // 中心线
        painter.setPen(QPen(QColor(80, 80, 80), 1));
        painter.drawLine(0, centerY, w, centerY);

        // 提取该声道的采样（交错存储）
        QVector<float> channelSamples;
        channelSamples.reserve(samplesPerChannel);
        for (int i = 0; i < samplesPerChannel; ++i) {
            int idx = i * channels + ch;
            if (idx < m_data.samples.size())
                channelSamples.append(m_data.samples[idx]);
        }

        // 自适应幅度：找到该声道的峰值，放大波形以填满显示区域
        float peakAbs = 0.0f;
        for (float s : channelSamples) {
            float a = std::fabs(s);
            if (a > peakAbs) peakAbs = a;
        }
        // 归一化系数：将峰值放大到 ~90% 显示区域，最小增益 1.0（不缩小）
        float gain = 1.0f;
        if (peakAbs > 1e-6f) {
            gain = 0.9f / peakAbs;
            if (gain < 1.0f) gain = 1.0f; // 如果信号已经接近满幅，不缩小
        }

        // 降采样
        auto pairs = downsample(channelSamples.constData(), channelSamples.size(), w);

        // 绘制波形
        QColor color = channelColors[ch % 4];
        painter.setPen(QPen(color, 1));

        if (pairs.size() <= w) {
            // 绘制 min/max 柱状（应用增益）
            for (int x = 0; x < pairs.size(); ++x) {
                float yMin = centerY - pairs[x].maxVal * gain * halfH;
                float yMax = centerY - pairs[x].minVal * gain * halfH;
                painter.drawLine(QPointF(x, yMin), QPointF(x, yMax));
            }
        }

        // 右下角标注增益（当放大超过 1.5x 时提示用户）
        if (gain > 1.5f) {
            painter.setPen(QColor(120, 120, 120));
            QFont smallFont = painter.font();
            smallFont.setPointSize(7);
            painter.setFont(smallFont);
            QString gainStr = QStringLiteral("×%1 (峰值: %2dB)")
                                  .arg(gain, 0, 'f', 1)
                                  .arg(peakAbs > 1e-10f ? 20.0f * std::log10(peakAbs) : -100.0f, 0, 'f', 1);
            QFontMetrics gfm(smallFont);
            painter.drawText(w - gfm.horizontalAdvance(gainStr) - 6,
                             yBase + channelHeight - 4, gainStr);
        }
    }
}
