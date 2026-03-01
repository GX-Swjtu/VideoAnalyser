#include "avsyncchartwidget.h"
#include "packetreader.h"
#include "themeutils.h"

#include <QApplication>
#include <QStyleHints>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <algorithm>
#include <cmath>
#include <limits>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

extern "C" {
#include <libavutil/avutil.h>
}

AVSyncChartWidget::AVSyncChartWidget(QWidget *parent)
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
        "<span style='color:#3366CC;font-weight:bold;'>&#8212; adts - vdts</span>"));
    toolbar->addWidget(legendLabel);
    toolbar->addStretch();
    mainLayout->addLayout(toolbar);

    // 图表
    m_chart = new QChart();
    m_chart->setTitle(QStringLiteral("Audio vs Video Sync"));
    m_chart->legend()->hide();
    m_chart->setAnimationOptions(QChart::NoAnimation);

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);
    mainLayout->addWidget(m_chartView, 1);

    // 初始化图表主题
    m_chart->setTheme(isDarkMode() ? QChart::ChartThemeDark : QChart::ChartThemeLight);

    connect(m_xAxisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AVSyncChartWidget::onXAxisModeChanged);
    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &AVSyncChartWidget::onColorSchemeChanged);
}

void AVSyncChartWidget::setPackets(const QVector<PacketInfo> &packets,
                                    const QVector<StreamInfo> &streams)
{
    m_packets = packets;
    m_streams = streams;
    rebuildChart();
}

void AVSyncChartWidget::clear()
{
    m_packets.clear();
    m_streams.clear();
    m_chart->removeAllSeries();
    const auto axes = m_chart->axes();
    for (auto *axis : axes)
        m_chart->removeAxis(axis);
}

void AVSyncChartWidget::onXAxisModeChanged(int /*index*/)
{
    rebuildChart();
}

void AVSyncChartWidget::onColorSchemeChanged()
{
    m_chart->setTheme(isDarkMode() ? QChart::ChartThemeDark : QChart::ChartThemeLight);
    rebuildChart();
}

void AVSyncChartWidget::rebuildChart()
{
    m_chart->removeAllSeries();
    const auto axes = m_chart->axes();
    for (auto *axis : axes)
        m_chart->removeAxis(axis);

    // 根据当前主题设置图表主题
    m_chart->setTheme(isDarkMode() ? QChart::ChartThemeDark : QChart::ChartThemeLight);

    // 查找首个视频流和音频流
    int videoIdx = -1, audioIdx = -1;
    for (const auto &s : m_streams) {
        if (s.mediaType == AVMEDIA_TYPE_VIDEO && videoIdx < 0) videoIdx = s.index;
        if (s.mediaType == AVMEDIA_TYPE_AUDIO && audioIdx < 0) audioIdx = s.index;
    }
    if (videoIdx < 0 || audioIdx < 0 || m_packets.isEmpty()) return;

    bool useOffset = (m_xAxisCombo->currentIndex() == 0);

    auto pts = buildSyncDeltaSeries(m_packets, videoIdx, audioIdx, useOffset);
    if (pts.isEmpty()) return;

    auto *series = new QLineSeries();
    series->setName(QStringLiteral("adts - vdts"));
    series->setColor(QColor(0x33, 0x66, 0xCC));
    series->setPen(QPen(QColor(0x33, 0x66, 0xCC), 1));
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
    axisY->setTitleText(QStringLiteral("(ms)"));
    axisY->setLabelFormat(QStringLiteral("%.0f"));

    m_chart->addAxis(axisX, Qt::AlignBottom);
    m_chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);
}

// ===== 静态方法 =====

QVector<QPointF> AVSyncChartWidget::buildSyncDeltaSeries(
    const QVector<PacketInfo> &packets,
    int videoStreamIndex, int audioStreamIndex,
    bool useOffset)
{
    // 按文件/mux 顺序遍历 packet，跟踪最近一个视频 DTS，
    // 在每个音频 packet 处计算 delta = audio_dts - last_video_dts (ms)。
    // 对同步良好的文件，delta 在 ±1 个视频帧时长内波动；
    // 若存在漂移，delta 的基线会随时间偏移。
    QVector<QPointF> points;
    points.reserve(packets.size() / 2);

    double lastVideoDts = std::numeric_limits<double>::quiet_NaN();

    for (const auto &p : packets) {
        if (p.streamIndex == videoStreamIndex && p.mediaType == AVMEDIA_TYPE_VIDEO) {
            lastVideoDts = p.dtsTime;
        } else if (p.streamIndex == audioStreamIndex && p.mediaType == AVMEDIA_TYPE_AUDIO) {
            if (std::isnan(lastVideoDts)) continue; // 尚无视频 DTS

            double deltaMs = (p.dtsTime - lastVideoDts) * 1000.0;
            double x = useOffset ? (p.pos >= 0 ? p.pos / (1024.0 * 1024.0) : 0.0)
                                 : p.dtsTime;
            points.append(QPointF(x, deltaMs));
        }
    }
    return points;
}

double AVSyncChartWidget::interpolateVideoDts(const QVector<double> &videoTimeSeries,
                                               double audioDtsTime)
{
    if (videoTimeSeries.isEmpty()) return audioDtsTime;

    // 二分查找第一个 >= audioDtsTime 的位置
    auto it = std::lower_bound(videoTimeSeries.begin(), videoTimeSeries.end(), audioDtsTime);

    if (it == videoTimeSeries.begin()) {
        return *it;
    }
    if (it == videoTimeSeries.end()) {
        return videoTimeSeries.last();
    }

    // 线性插值
    auto prev = it - 1;
    double t0 = *prev;
    double t1 = *it;
    if (qFuzzyCompare(t0, t1)) return t0;

    double ratio = (audioDtsTime - t0) / (t1 - t0);
    return t0 + ratio * (t1 - t0);
}
