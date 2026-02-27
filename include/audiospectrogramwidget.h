#ifndef AUDIOSPECTROGRAMWIDGET_H
#define AUDIOSPECTROGRAMWIDGET_H

#include <QWidget>
#include <QVector>
#include <QImage>
#include "audiowaveformwidget.h"  // for AudioData

class AudioSpectrogramWidget : public QWidget {
    Q_OBJECT
public:
    explicit AudioSpectrogramWidget(QWidget *parent = nullptr);

    void setAudioData(const AudioData &data);
    const AudioData &audioData() const { return m_data; }

    // --- 公开静态方法，方便单元测试 ---

    // 多声道交错数据混缩为单声道（取平均）
    static QVector<float> mixToMono(const QVector<float> &interleaved, int channels);

    // 计算频谱图矩阵：返回 [timeFrame][freqBin] 的幅度 dB 值
    // fftSize 必须是 2 的幂，hopSize 是帧移（通常 fftSize/2）
    static QVector<QVector<float>> computeSpectrogram(
        const float *monoSamples, int sampleCount, int fftSize, int hopSize);

    // 计算峰值电平 (dB)
    static float computePeakDb(const QVector<float> &samples);

    // 计算 RMS 电平 (dB)
    static float computeRmsDb(const QVector<float> &samples);

    // 生成 Hanning 窗
    static QVector<float> generateHanningWindow(int size);

    // dB 值映射为颜色（-90dB ~ 0dB → 黑→蓝→紫→橙→白）
    static QRgb dbToColor(float db);

    // 自适应频率范围：找到有能量的最高频率 bin 索引
    // 返回值为 bin 索引（0 ~ numBins-1），energyThresholdDb 为判定阈值
    static int findMaxFrequencyBin(const QVector<QVector<float>> &specData, float energyThresholdDb = -70.0f);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuildSpectrogram();

    AudioData m_data;
    QImage m_spectrogramImage;   // 预计算的频谱热力图（已裁剪到有效频率范围）
    float m_peakDb = -100.0f;
    float m_rmsDb = -100.0f;
    float m_displayMaxFreq = 0.0f;  // 实际显示的最高频率 (Hz)

    static constexpr int FFT_SIZE = 256;
    static constexpr int HOP_SIZE = 128;   // 50% overlap
    static constexpr float DB_MIN = -90.0f;
    static constexpr float DB_MAX = 0.0f;
    static constexpr float SILENCE_THRESHOLD_DB = -50.0f;
    static constexpr float FREQ_ENERGY_THRESHOLD_DB = -70.0f;  // 自适应频率范围的能量阈值
    static constexpr float MIN_DISPLAY_FREQ_RATIO = 0.1f;      // 最少显示 10% 的频率范围
};

#endif // AUDIOSPECTROGRAMWIDGET_H
