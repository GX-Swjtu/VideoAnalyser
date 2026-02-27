#ifndef TIMESTAMPCHARTWIDGET_H
#define TIMESTAMPCHARTWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPointF>

struct PacketInfo;

extern "C" {
#include <libavutil/avutil.h>
}

class QComboBox;
class QChart;
class QChartView;

class TimestampChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimestampChartWidget(QWidget *parent = nullptr);

    /// 设置 packet 数据并重建图表
    void setPackets(const QVector<PacketInfo> &packets);

    /// 清空图表
    void clear();

    // ===== 静态方法（公开供测试） =====

    /// DTS 异常类型
    struct TimestampAnomaly {
        int packetIndex;       // 出现异常的 packet 序号
        double prevDtsMs;      // 上一个 DTS (ms)
        double currDtsMs;      // 当前 DTS (ms)
        QString type;          // "jump" = 正向跳变, "rollback" = 回跳
    };

    /// 构建指定媒体类型的时间戳序列
    /// useOffset=true → X 为文件偏移(MB)，useOffset=false → X 为时间(秒)
    static QVector<QPointF> buildTimestampSeries(const QVector<PacketInfo> &packets,
                                                  int mediaType,
                                                  bool useOffset);

    /// 检测 DTS 异常（跳变 / 回跳）
    static QVector<TimestampAnomaly> detectAnomalies(const QVector<PacketInfo> &packets,
                                                      int mediaType,
                                                      double jumpThresholdMs = 1000.0);

private slots:
    void onXAxisModeChanged(int index);

private:
    void rebuildChart();

    QComboBox *m_xAxisCombo = nullptr;
    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    QVector<PacketInfo> m_packets;
};

#endif // TIMESTAMPCHARTWIDGET_H
