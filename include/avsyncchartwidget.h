#ifndef AVSYNCCHARTWIDGET_H
#define AVSYNCCHARTWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QPair>

struct PacketInfo;
struct StreamInfo;

class QComboBox;
class QChart;
class QChartView;

class AVSyncChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit AVSyncChartWidget(QWidget *parent = nullptr);

    /// 设置 packet 数据并重建图表
    void setPackets(const QVector<PacketInfo> &packets, const QVector<StreamInfo> &streams);

    /// 清空图表
    void clear();

    // ===== 静态方法（公开供测试） =====

    /// 构建音视频同步差值序列 (x, delta_ms)
    /// delta = audio_dtsTime - interpolated_video_dtsTime (ms)
    static QVector<QPointF> buildSyncDeltaSeries(const QVector<PacketInfo> &packets,
                                                  int videoStreamIndex,
                                                  int audioStreamIndex,
                                                  bool useOffset);

    /// 在视频 (dtsTime, dtsTime) 序列中对指定 audioDtsTime 进行线性插值
    /// videoTimeSeries: 已排序的 (dtsTime) 列表
    /// 返回插值后的 video dtsTime，单位秒
    static double interpolateVideoDts(const QVector<double> &videoTimeSeries,
                                       double audioDtsTime);

private slots:
    void onXAxisModeChanged(int index);

private:
    void rebuildChart();

    QComboBox *m_xAxisCombo = nullptr;
    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    QVector<PacketInfo> m_packets;
    QVector<StreamInfo> m_streams;
};

#endif // AVSYNCCHARTWIDGET_H
