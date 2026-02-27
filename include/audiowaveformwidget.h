#ifndef AUDIOWAVEFORMWIDGET_H
#define AUDIOWAVEFORMWIDGET_H

#include <QWidget>
#include <QVector>

struct AudioData {
    QVector<float> samples;  // 交错排列的浮点采样（-1.0 ~ 1.0）
    int sampleRate = 0;
    int channels = 0;
};

class AudioWaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit AudioWaveformWidget(QWidget *parent = nullptr);

    void setAudioData(const AudioData &data);
    const AudioData &audioData() const { return m_data; }

    // 公开供测试：降采样后的 min/max 对
    struct MinMaxPair {
        float minVal;
        float maxVal;
    };
    static QVector<MinMaxPair> downsample(const float *samples, int count, int targetWidth);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    AudioData m_data;
};

#endif // AUDIOWAVEFORMWIDGET_H
