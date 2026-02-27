#include "audiospectrogramwidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <cmath>
#include <algorithm>

extern "C" {
#include <libavutil/tx.h>
#include <libavutil/mem.h>
}

AudioSpectrogramWidget::AudioSpectrogramWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
    setMinimumWidth(200);
}

void AudioSpectrogramWidget::setAudioData(const AudioData &data)
{
    m_data = data;
    rebuildSpectrogram();
    update();
}

// --- 静态方法 ---

QVector<float> AudioSpectrogramWidget::mixToMono(const QVector<float> &interleaved, int channels)
{
    if (channels <= 0 || interleaved.isEmpty()) return {};
    if (channels == 1) return interleaved;

    int samplesPerChannel = interleaved.size() / channels;
    QVector<float> mono(samplesPerChannel);
    float invChannels = 1.0f / channels;

    for (int i = 0; i < samplesPerChannel; ++i) {
        float sum = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            int idx = i * channels + ch;
            if (idx < interleaved.size())
                sum += interleaved[idx];
        }
        mono[i] = sum * invChannels;
    }
    return mono;
}

QVector<float> AudioSpectrogramWidget::generateHanningWindow(int size)
{
    QVector<float> window(size);
    for (int i = 0; i < size; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / size));
    }
    return window;
}

float AudioSpectrogramWidget::computePeakDb(const QVector<float> &samples)
{
    if (samples.isEmpty()) return -100.0f;

    float peak = 0.0f;
    for (float s : samples) {
        float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    if (peak < 1e-10f) return -100.0f;
    return 20.0f * std::log10(peak);
}

float AudioSpectrogramWidget::computeRmsDb(const QVector<float> &samples)
{
    if (samples.isEmpty()) return -100.0f;

    double sumSq = 0.0;
    for (float s : samples) {
        sumSq += static_cast<double>(s) * s;
    }
    double rms = std::sqrt(sumSq / samples.size());
    if (rms < 1e-10) return -100.0f;
    return 20.0f * static_cast<float>(std::log10(rms));
}

QVector<QVector<float>> AudioSpectrogramWidget::computeSpectrogram(
    const float *monoSamples, int sampleCount, int fftSize, int hopSize)
{
    QVector<QVector<float>> result;
    if (!monoSamples || sampleCount <= 0 || fftSize <= 0 || hopSize <= 0)
        return result;

    int numBins = fftSize / 2 + 1;
    QVector<float> window = generateHanningWindow(fftSize);

    // 初始化 FFmpeg RDFT
    AVTXContext *txCtx = nullptr;
    av_tx_fn txFn = nullptr;
    float scale = 1.0f;
    int ret = av_tx_init(&txCtx, &txFn, AV_TX_FLOAT_RDFT, 0, fftSize, &scale, 0);
    if (ret < 0 || !txCtx || !txFn) {
        // FFT 初始化失败，返回空
        return result;
    }

    // RDFT 输入需要 fftSize 个 float，输出需要 numBins 个 AVComplexFloat
    // av_tx 要求内存对齐，使用 av_malloc
    float *inBuf = static_cast<float*>(av_malloc(sizeof(float) * fftSize));
    AVComplexFloat *outBuf = static_cast<AVComplexFloat*>(av_malloc(sizeof(AVComplexFloat) * numBins));

    if (!inBuf || !outBuf) {
        av_free(inBuf);
        av_free(outBuf);
        av_tx_uninit(&txCtx);
        return result;
    }

    for (int frameStart = 0; frameStart + fftSize <= sampleCount; frameStart += hopSize) {
        // 加窗
        for (int i = 0; i < fftSize; ++i) {
            inBuf[i] = monoSamples[frameStart + i] * window[i];
        }

        // 执行 RDFT
        txFn(txCtx, outBuf, inBuf, sizeof(float));

        // 计算幅度 dB
        QVector<float> frame(numBins);
        for (int i = 0; i < numBins; ++i) {
            float mag = std::sqrt(outBuf[i].re * outBuf[i].re + outBuf[i].im * outBuf[i].im);
            // 归一化
            mag /= fftSize;
            // 转 dB
            if (mag < 1e-10f) {
                frame[i] = -100.0f;
            } else {
                frame[i] = 20.0f * std::log10(mag);
            }
        }
        result.append(frame);
    }

    av_free(inBuf);
    av_free(outBuf);
    av_tx_uninit(&txCtx);

    return result;
}

QRgb AudioSpectrogramWidget::dbToColor(float db)
{
    // 将 dB 映射到 0.0~1.0
    float t = (db - DB_MIN) / (DB_MAX - DB_MIN);
    t = std::clamp(t, 0.0f, 1.0f);

    // 5 段渐变色带：黑→深蓝→紫红→橙黄→白
    // 控制点: t=0.0 黑, t=0.25 深蓝, t=0.5 紫红, t=0.75 橙黄, t=1.0 白
    struct ColorStop { float t; int r, g, b; };
    static const ColorStop stops[] = {
        {0.00f,   0,   0,   0},   // 黑
        {0.20f,   0,   0, 120},   // 深蓝
        {0.40f, 100,   0, 180},   // 紫
        {0.60f, 200,  30,  60},   // 红
        {0.80f, 255, 180,   0},   // 橙黄
        {1.00f, 255, 255, 255},   // 白
    };
    static const int nStops = 6;

    // 找到 t 所在的区间
    int seg = 0;
    for (int i = 1; i < nStops; ++i) {
        if (t <= stops[i].t) { seg = i - 1; break; }
        if (i == nStops - 1) seg = nStops - 2;
    }

    float segT = (t - stops[seg].t) / (stops[seg + 1].t - stops[seg].t);
    segT = std::clamp(segT, 0.0f, 1.0f);

    int r = static_cast<int>(stops[seg].r + segT * (stops[seg + 1].r - stops[seg].r));
    int g = static_cast<int>(stops[seg].g + segT * (stops[seg + 1].g - stops[seg].g));
    int b = static_cast<int>(stops[seg].b + segT * (stops[seg + 1].b - stops[seg].b));

    return qRgb(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
}

int AudioSpectrogramWidget::findMaxFrequencyBin(const QVector<QVector<float>> &specData, float energyThresholdDb)
{
    if (specData.isEmpty()) return 0;

    int numBins = specData[0].size();
    int maxBin = 0;

    // 扫描所有时间帧，找有能量的最高频率 bin
    for (const auto &frame : specData) {
        for (int i = numBins - 1; i > maxBin; --i) {
            if (frame[i] > energyThresholdDb) {
                maxBin = i;
                break;
            }
        }
    }
    return maxBin;
}

// --- 内部方法 ---

void AudioSpectrogramWidget::rebuildSpectrogram()
{
    m_spectrogramImage = QImage();
    m_peakDb = -100.0f;
    m_rmsDb = -100.0f;
    m_displayMaxFreq = 0.0f;

    if (m_data.samples.isEmpty() || m_data.channels <= 0) return;

    // 计算电平
    m_peakDb = computePeakDb(m_data.samples);
    m_rmsDb = computeRmsDb(m_data.samples);

    // 混缩为单声道
    QVector<float> mono = mixToMono(m_data.samples, m_data.channels);
    if (mono.isEmpty()) return;

    // 计算频谱图
    auto specData = computeSpectrogram(mono.constData(), mono.size(), FFT_SIZE, HOP_SIZE);
    if (specData.isEmpty()) return;

    int timeFrames = specData.size();
    int totalBins = specData[0].size();

    // 自适应频率范围：找有效能量的最高频率 bin，加 20% 余量
    int maxBin = findMaxFrequencyBin(specData, FREQ_ENERGY_THRESHOLD_DB);
    int displayBins = static_cast<int>(maxBin * 1.2f) + 2; // 加 20% 余量 + 2 bin 边距

    // 至少显示总范围的 10%
    int minBins = static_cast<int>(totalBins * MIN_DISPLAY_FREQ_RATIO);
    displayBins = std::clamp(displayBins, std::max(minBins, 4), totalBins);

    // 计算实际显示的最高频率
    float nyquist = m_data.sampleRate / 2.0f;
    m_displayMaxFreq = nyquist * displayBins / totalBins;

    // 生成热力图 QImage（只包含 0 ~ displayBins 的频率范围）
    m_spectrogramImage = QImage(timeFrames, displayBins, QImage::Format_RGB32);
    for (int x = 0; x < timeFrames; ++x) {
        for (int y = 0; y < displayBins; ++y) {
            // y=0 → 显示范围内最高频率（图像顶部），y=displayBins-1 → 0Hz（图像底部）
            int freqIdx = displayBins - 1 - y;
            float db = (freqIdx < specData[x].size()) ? specData[x][freqIdx] : -100.0f;
            m_spectrogramImage.setPixel(x, y, dbToColor(db));
        }
    }
}

void AudioSpectrogramWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 背景
    painter.fillRect(rect(), QColor(20, 20, 20));

    if (m_data.samples.isEmpty() || m_data.channels <= 0) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No audio data"));
        return;
    }

    int w = width();
    int h = height();

    // 布局参数
    int infoBarHeight = 20;
    int freqLabelWidth = 50;
    int bottomMargin = 4;
    int spectrogramX = freqLabelWidth;
    int spectrogramY = infoBarHeight + 2;
    int spectrogramW = w - freqLabelWidth - 4;
    int spectrogramH = h - infoBarHeight - bottomMargin - 4;

    if (spectrogramW <= 0 || spectrogramH <= 0) return;

    // --- 信息栏 ---
    painter.setPen(QColor(200, 200, 200));
    QFont infoFont = painter.font();
    infoFont.setPointSize(9);
    painter.setFont(infoFont);

    int samplesPerChannel = m_data.samples.size() / m_data.channels;
    double duration = (samplesPerChannel > 0 && m_data.sampleRate > 0)
                          ? static_cast<double>(samplesPerChannel) / m_data.sampleRate
                          : 0.0;

    bool isSilent = m_rmsDb < SILENCE_THRESHOLD_DB;
    QString silenceStr = isSilent ? QStringLiteral("静音") : QStringLiteral("有声音");
    QColor silenceColor = isSilent ? QColor(255, 80, 80) : QColor(80, 255, 80);

    QString info = QStringLiteral("峰值: %1dB | RMS: %2dB | ")
                       .arg(m_peakDb, 0, 'f', 1)
                       .arg(m_rmsDb, 0, 'f', 1);
    QString info2 = QStringLiteral(" | %1Hz | %2ch | %3s")
                        .arg(m_data.sampleRate)
                        .arg(m_data.channels)
                        .arg(duration, 0, 'f', 3);

    // 绘制信息文本（静音/有声音用彩色）
    int textX = 8;
    QFontMetrics fm(painter.font());
    painter.drawText(textX, 14, info);
    textX += fm.horizontalAdvance(info);

    painter.setPen(silenceColor);
    QFont boldFont = painter.font();
    boldFont.setBold(true);
    painter.setFont(boldFont);
    painter.drawText(textX, 14, silenceStr);
    textX += fm.horizontalAdvance(silenceStr);

    boldFont.setBold(false);
    painter.setFont(boldFont);
    painter.setPen(QColor(200, 200, 200));
    painter.drawText(textX, 14, info2);

    // --- 频谱图 ---
    if (!m_spectrogramImage.isNull()) {
        QImage scaled = m_spectrogramImage.scaled(
            spectrogramW, spectrogramH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(spectrogramX, spectrogramY, scaled);
    } else {
        painter.setPen(QColor(100, 100, 100));
        painter.drawText(QRect(spectrogramX, spectrogramY, spectrogramW, spectrogramH),
                         Qt::AlignCenter, QStringLiteral("频谱数据不足"));
    }

    // --- 频率刻度（自适应范围）---
    painter.setPen(QColor(150, 150, 150));
    QFont scaleFont = painter.font();
    scaleFont.setPointSize(8);
    painter.setFont(scaleFont);

    if (m_data.sampleRate > 0 && m_displayMaxFreq > 0) {
        // 根据显示的最高频率选择合适的刻度间距
        float maxFreq = m_displayMaxFreq;

        // 选择刻度间距：使得大约有 4-6 个刻度
        static const float niceSteps[] = {100, 200, 500, 1000, 2000, 2500, 5000, 10000, 20000};
        float step = niceSteps[0];
        for (float s : niceSteps) {
            if (maxFreq / s <= 6) { step = s; break; }
        }

        // 绘制刻度
        for (float freq = 0; freq <= maxFreq; freq += step) {
            float yFrac = 1.0f - freq / maxFreq;  // 0Hz 在底部
            int y = spectrogramY + static_cast<int>(yFrac * spectrogramH);

            QString label;
            if (freq >= 1000.0f) {
                label = QStringLiteral("%1kHz").arg(freq / 1000.0f, 0, 'f', 1);
            } else {
                label = QStringLiteral("%1Hz").arg(static_cast<int>(freq));
            }
            painter.drawText(2, y + 4, label);

            // 刻度横线
            painter.setPen(QPen(QColor(60, 60, 60), 1, Qt::DotLine));
            painter.drawLine(spectrogramX, y, spectrogramX + spectrogramW, y);
            painter.setPen(QColor(150, 150, 150));
        }

        // 顶部标注实际最高频率
        int yTop = spectrogramY;
        QString topLabel;
        if (maxFreq >= 1000.0f) {
            topLabel = QStringLiteral("%1kHz").arg(maxFreq / 1000.0f, 0, 'f', 1);
        } else {
            topLabel = QStringLiteral("%1Hz").arg(static_cast<int>(maxFreq));
        }
        painter.drawText(2, yTop + 4, topLabel);
    }

    // --- 频谱图边框 ---
    painter.setPen(QPen(QColor(80, 80, 80), 1));
    painter.drawRect(spectrogramX, spectrogramY, spectrogramW - 1, spectrogramH - 1);
}
