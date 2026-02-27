#ifndef BITRATECHARTWIDGET_H
#define BITRATECHARTWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPointF>

struct PacketInfo;
struct StreamInfo;

class QComboBox;
class QChart;
class QChartView;

class BitrateChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit BitrateChartWidget(QWidget *parent = nullptr);

    /// 设置 packet 数据并重建图表
    void setPackets(const QVector<PacketInfo> &packets, const QVector<StreamInfo> &streams);

    /// 清空图表
    void clear();

    // ===== 静态方法（公开供测试） =====

    /// 构建视频帧码率序列 (x, bitrate_Mbps)
    /// useOffset=true → X 为文件偏移(MB)，false → X 为时间(秒)
    static QVector<QPointF> buildBitrateSeries(const QVector<PacketInfo> &packets,
                                                int videoStreamIndex,
                                                bool useOffset);

    /// 计算整体平均码率 (Mbps)
    static double computeAverageBitrate(const QVector<PacketInfo> &packets,
                                         int videoStreamIndex);

private slots:
    void onXAxisModeChanged(int index);

private:
    void rebuildChart();
    int findVideoStreamIndex() const;

    QComboBox *m_xAxisCombo = nullptr;
    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    QVector<PacketInfo> m_packets;
    QVector<StreamInfo> m_streams;
};

#endif // BITRATECHARTWIDGET_H
