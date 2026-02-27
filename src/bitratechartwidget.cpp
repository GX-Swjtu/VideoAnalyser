#include "bitratechartwidget.h"
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

BitrateChartWidget::BitrateChartWidget(QWidget *parent)
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

    toolbar->addSpacing(20);
    auto *legendLabel = new QLabel(QStringLiteral(
        "<span style='color:#22AA22;font-weight:bold;'>&#8212; rate(Mbps)</span>"));
    toolbar->addWidget(legendLabel);
    toolbar->addStretch();
    mainLayout->addLayout(toolbar);

    // 图表
    m_chart = new QChart();
    m_chart->setTitle(QStringLiteral("Rate based on video"));
    m_chart->legend()->hide();
    m_chart->setAnimationOptions(QChart::NoAnimation);

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);
    mainLayout->addWidget(m_chartView, 1);

    connect(m_xAxisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BitrateChartWidget::onXAxisModeChanged);
}

void BitrateChartWidget::setPackets(const QVector<PacketInfo> &packets,
                                     const QVector<StreamInfo> &streams)
{
    m_packets = packets;
    m_streams = streams;
    rebuildChart();
}

void BitrateChartWidget::clear()
{
    m_packets.clear();
    m_streams.clear();
    m_chart->removeAllSeries();
    const auto axes = m_chart->axes();
    for (auto *axis : axes)
        m_chart->removeAxis(axis);
}

void BitrateChartWidget::onXAxisModeChanged(int /*index*/)
{
    rebuildChart();
}

int BitrateChartWidget::findVideoStreamIndex() const
{
    for (const auto &s : m_streams) {
        if (s.mediaType == AVMEDIA_TYPE_VIDEO) return s.index;
    }
    return -1;
}

void BitrateChartWidget::rebuildChart()
{
    m_chart->removeAllSeries();
    const auto axes = m_chart->axes();
    for (auto *axis : axes)
        m_chart->removeAxis(axis);

    int videoIdx = findVideoStreamIndex();
    if (videoIdx < 0 || m_packets.isEmpty()) return;

    bool useOffset = (m_xAxisCombo->currentIndex() == 0);

    auto pts = buildBitrateSeries(m_packets, videoIdx, useOffset);
    auto *series = new QLineSeries();
    series->setName(QStringLiteral("bitrate"));
    series->setColor(QColor(0x22, 0xAA, 0x22));
    QPen pen(QColor(0x22, 0xAA, 0x22), 1);
    series->setPen(pen);
    for (const auto &pt : pts)
        series->append(pt);

    m_chart->addSeries(series);

    auto *axisX = new QValueAxis();
    auto *axisY = new QValueAxis();

    if (useOffset) {
        axisX->setTitleText(QStringLiteral("pos (offset)"));
        axisX->setLabelFormat(QStringLiteral("%.1fMB"));
    } else {
        axisX->setTitleText(QStringLiteral("time (s)"));
        axisX->setLabelFormat(QStringLiteral("%.1f"));
    }
    axisY->setTitleText(QStringLiteral("rate(Mbps)"));
    axisY->setLabelFormat(QStringLiteral("%.2f"));

    m_chart->addAxis(axisX, Qt::AlignBottom);
    m_chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);
}

// ===== 静态方法 =====

QVector<QPointF> BitrateChartWidget::buildBitrateSeries(
    const QVector<PacketInfo> &packets, int videoStreamIndex, bool useOffset)
{
    QVector<QPointF> points;
    points.reserve(packets.size() / 2);

    for (const auto &p : packets) {
        if (p.streamIndex != videoStreamIndex) continue;
        if (p.mediaType != AVMEDIA_TYPE_VIDEO) continue;

        double x = useOffset ? (p.pos >= 0 ? p.pos / (1024.0 * 1024.0) : 0.0)
                             : p.dtsTime;

        // 瞬时码率 = (size * 8) / durationTime / 1e6
        double bitrateMbps = 0.0;
        if (p.durationTime > 0.0) {
            bitrateMbps = (p.size * 8.0) / p.durationTime / 1000000.0;
        }

        points.append(QPointF(x, bitrateMbps));
    }
    return points;
}

double BitrateChartWidget::computeAverageBitrate(
    const QVector<PacketInfo> &packets, int videoStreamIndex)
{
    int64_t totalBytes = 0;
    double totalDuration = 0.0;

    for (const auto &p : packets) {
        if (p.streamIndex != videoStreamIndex) continue;
        if (p.mediaType != AVMEDIA_TYPE_VIDEO) continue;
        totalBytes += p.size;
        totalDuration += p.durationTime;
    }

    if (totalDuration <= 0.0) return 0.0;
    return (totalBytes * 8.0) / totalDuration / 1000000.0;
}
