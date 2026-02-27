#include "timestampchartwidget.h"
#include "packetreader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

extern "C" {
#include <libavutil/avutil.h>
}

TimestampChartWidget::TimestampChartWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // 工具栏
    auto *toolbar = new QHBoxLayout();
    toolbar->addWidget(new QLabel(QStringLiteral("X 轴:")));
    m_xAxisCombo = new QComboBox();
    m_xAxisCombo->addItem(QStringLiteral("文件偏移 (MB)"));
    m_xAxisCombo->addItem(QStringLiteral("时间 (秒)"));
    m_xAxisCombo->setCurrentIndex(1);
    toolbar->addWidget(m_xAxisCombo);

    // 图例
    toolbar->addSpacing(20);
    auto *legendLabel = new QLabel(QStringLiteral(
        "<span style='color:#3366CC;font-weight:bold;'>&#9650; vdts</span>"
        "&nbsp;&nbsp;"
        "<span style='color:#FF8800;font-weight:bold;'>+ adts</span>"));
    toolbar->addWidget(legendLabel);
    toolbar->addStretch();
    mainLayout->addLayout(toolbar);

    // 图表
    m_chart = new QChart();
    m_chart->setTitle(QStringLiteral("TimeStamp Details"));
    m_chart->legend()->hide();
    m_chart->setAnimationOptions(QChart::NoAnimation);

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);
    mainLayout->addWidget(m_chartView, 1);

    connect(m_xAxisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TimestampChartWidget::onXAxisModeChanged);
}

void TimestampChartWidget::setPackets(const QVector<PacketInfo> &packets)
{
    m_packets = packets;
    rebuildChart();
}

void TimestampChartWidget::clear()
{
    m_packets.clear();
    m_chart->removeAllSeries();
    // 清除坐标轴
    const auto axes = m_chart->axes();
    for (auto *axis : axes)
        m_chart->removeAxis(axis);
}

void TimestampChartWidget::onXAxisModeChanged(int /*index*/)
{
    rebuildChart();
}

void TimestampChartWidget::rebuildChart()
{
    m_chart->removeAllSeries();
    const auto axes = m_chart->axes();
    for (auto *axis : axes)
        m_chart->removeAxis(axis);

    if (m_packets.isEmpty()) return;

    bool useOffset = (m_xAxisCombo->currentIndex() == 0);

    // 视频 DTS 系列
    auto videoPts = buildTimestampSeries(m_packets, AVMEDIA_TYPE_VIDEO, useOffset);
    auto *videoSeries = new QLineSeries();
    videoSeries->setName(QStringLiteral("vdts"));
    videoSeries->setColor(QColor(0x33, 0x66, 0xCC));
    videoSeries->setPen(QPen(QColor(0x33, 0x66, 0xCC), 1));
    for (const auto &pt : videoPts)
        videoSeries->append(pt);

    // 音频 DTS 系列
    auto audioPts = buildTimestampSeries(m_packets, AVMEDIA_TYPE_AUDIO, useOffset);
    auto *audioSeries = new QLineSeries();
    audioSeries->setName(QStringLiteral("adts"));
    audioSeries->setColor(QColor(0xFF, 0x88, 0x00));
    audioSeries->setPen(QPen(QColor(0xFF, 0x88, 0x00), 1));
    for (const auto &pt : audioPts)
        audioSeries->append(pt);

    m_chart->addSeries(videoSeries);
    m_chart->addSeries(audioSeries);

    // 坐标轴
    auto *axisX = new QValueAxis();
    auto *axisY = new QValueAxis();

    if (useOffset) {
        axisX->setTitleText(QStringLiteral("pos (offset)"));
        axisX->setLabelFormat(QStringLiteral("%.1fMB"));
    } else {
        axisX->setTitleText(QStringLiteral("time (s)"));
        axisX->setLabelFormat(QStringLiteral("%.1f"));
    }
    axisY->setTitleText(QStringLiteral("(ms)"));
    axisY->setLabelFormat(QStringLiteral("%.0f"));

    m_chart->addAxis(axisX, Qt::AlignBottom);
    m_chart->addAxis(axisY, Qt::AlignLeft);
    videoSeries->attachAxis(axisX);
    videoSeries->attachAxis(axisY);
    audioSeries->attachAxis(axisX);
    audioSeries->attachAxis(axisY);
}

// ===== 静态方法 =====

QVector<QPointF> TimestampChartWidget::buildTimestampSeries(
    const QVector<PacketInfo> &packets, int mediaType, bool useOffset)
{
    QVector<QPointF> points;
    points.reserve(packets.size() / 2);
    for (const auto &p : packets) {
        if (static_cast<int>(p.mediaType) != mediaType) continue;
        double x = useOffset ? (p.pos >= 0 ? p.pos / (1024.0 * 1024.0) : 0.0)
                             : p.dtsTime;
        double y = p.dtsTime * 1000.0; // DTS in ms
        points.append(QPointF(x, y));
    }
    return points;
}

QVector<TimestampChartWidget::TimestampAnomaly> TimestampChartWidget::detectAnomalies(
    const QVector<PacketInfo> &packets, int mediaType, double jumpThresholdMs)
{
    QVector<TimestampAnomaly> anomalies;
    double prevDtsMs = -1.0;
    bool hasPrev = false;

    for (const auto &p : packets) {
        if (static_cast<int>(p.mediaType) != mediaType) continue;

        double curDtsMs = p.dtsTime * 1000.0;

        if (hasPrev) {
            double delta = curDtsMs - prevDtsMs;
            if (delta < 0) {
                // 回跳
                anomalies.append({p.index, prevDtsMs, curDtsMs, QStringLiteral("rollback")});
            } else if (delta > jumpThresholdMs) {
                // 正向跳变
                anomalies.append({p.index, prevDtsMs, curDtsMs, QStringLiteral("jump")});
            }
        }
        prevDtsMs = curDtsMs;
        hasPrev = true;
    }
    return anomalies;
}
