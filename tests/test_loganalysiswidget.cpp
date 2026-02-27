#include <gtest/gtest.h>
#include "loganalysiswidget.h"

extern "C" {
#include <libavutil/log.h>
}

// ===== levelToString 测试 =====

TEST(LogAnalysisWidgetTest, LevelToString_Fatal) {
    EXPECT_EQ(LogAnalysisWidget::levelToString(AV_LOG_FATAL), "fatal");
}

TEST(LogAnalysisWidgetTest, LevelToString_Error) {
    EXPECT_EQ(LogAnalysisWidget::levelToString(AV_LOG_ERROR), "error");
}

TEST(LogAnalysisWidgetTest, LevelToString_Warning) {
    EXPECT_EQ(LogAnalysisWidget::levelToString(AV_LOG_WARNING), "warning");
}

TEST(LogAnalysisWidgetTest, LevelToString_Info) {
    EXPECT_EQ(LogAnalysisWidget::levelToString(AV_LOG_INFO), "info");
}

TEST(LogAnalysisWidgetTest, LevelToString_Verbose) {
    EXPECT_EQ(LogAnalysisWidget::levelToString(AV_LOG_VERBOSE), "verbose");
}

TEST(LogAnalysisWidgetTest, LevelToString_Debug) {
    EXPECT_EQ(LogAnalysisWidget::levelToString(AV_LOG_DEBUG), "debug");
}

TEST(LogAnalysisWidgetTest, LevelToString_Trace) {
    EXPECT_EQ(LogAnalysisWidget::levelToString(AV_LOG_TRACE), "trace");
}

// ===== levelToIcon 测试 =====

TEST(LogAnalysisWidgetTest, LevelToIcon_Error) {
    // 应返回 ✖ 字符
    QString icon = LogAnalysisWidget::levelToIcon(AV_LOG_ERROR);
    EXPECT_FALSE(icon.isEmpty());
    EXPECT_EQ(icon, "\u2716");
}

TEST(LogAnalysisWidgetTest, LevelToIcon_Warning) {
    QString icon = LogAnalysisWidget::levelToIcon(AV_LOG_WARNING);
    EXPECT_EQ(icon, "\u26A0");
}

TEST(LogAnalysisWidgetTest, LevelToIcon_Info) {
    QString icon = LogAnalysisWidget::levelToIcon(AV_LOG_INFO);
    EXPECT_EQ(icon, "\u2139");
}

TEST(LogAnalysisWidgetTest, LevelToIcon_Verbose) {
    QString icon = LogAnalysisWidget::levelToIcon(AV_LOG_VERBOSE);
    EXPECT_EQ(icon, "\u2022"); // •
}

// ===== LogTableModel 测试 =====

TEST(LogTableModelTest, EmptyModel) {
    LogTableModel model;
    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_EQ(model.columnCount(), LogTableModel::ColCount);
}

TEST(LogTableModelTest, AppendAndQuery) {
    LogTableModel model;

    QVector<LogEntry> entries;
    LogEntry e;
    e.index = 1;
    e.level = AV_LOG_WARNING;
    e.className = "h264";
    e.message = "test warning";
    e.timestamp = QDateTime::currentDateTime();
    entries.append(e);

    model.appendEntries(entries);
    EXPECT_EQ(model.rowCount(), 1);

    // DisplayRole 测试
    auto typeData = model.data(model.index(0, LogTableModel::ColType), Qt::DisplayRole);
    EXPECT_FALSE(typeData.toString().isEmpty());

    auto indexData = model.data(model.index(0, LogTableModel::ColIndex), Qt::DisplayRole);
    EXPECT_EQ(indexData.toInt(), 1);

    auto levelData = model.data(model.index(0, LogTableModel::ColLevel), Qt::DisplayRole);
    EXPECT_EQ(levelData.toString(), "warning");

    auto descData = model.data(model.index(0, LogTableModel::ColDescription), Qt::DisplayRole);
    EXPECT_EQ(descData.toString(), "test warning");
}

TEST(LogTableModelTest, ClearAll) {
    LogTableModel model;
    QVector<LogEntry> entries;
    LogEntry e{};
    e.index = 1;
    e.level = AV_LOG_INFO;
    e.message = "msg";
    e.timestamp = QDateTime::currentDateTime();
    entries.append(e);

    model.appendEntries(entries);
    EXPECT_EQ(model.rowCount(), 1);

    model.clearAll();
    EXPECT_EQ(model.rowCount(), 0);
}

TEST(LogTableModelTest, LevelAt) {
    LogTableModel model;
    QVector<LogEntry> entries;
    LogEntry e{};
    e.index = 1;
    e.level = AV_LOG_ERROR;
    e.message = "err";
    e.timestamp = QDateTime::currentDateTime();
    entries.append(e);

    model.appendEntries(entries);
    EXPECT_EQ(model.levelAt(0), AV_LOG_ERROR);
    EXPECT_EQ(model.levelAt(-1), AV_LOG_QUIET); // 越界
    EXPECT_EQ(model.levelAt(1), AV_LOG_QUIET);  // 越界
}

// ===== LogFilterProxyModel 测试 =====

TEST(LogFilterProxyModelTest, FilterByLevel) {
    LogTableModel model;
    LogFilterProxyModel proxy;
    proxy.setSourceModel(&model);

    QVector<LogEntry> entries;
    LogEntry e1{};
    e1.index = 1; e1.level = AV_LOG_ERROR; e1.message = "error"; e1.timestamp = QDateTime::currentDateTime();
    LogEntry e2{};
    e2.index = 2; e2.level = AV_LOG_WARNING; e2.message = "warn"; e2.timestamp = QDateTime::currentDateTime();
    LogEntry e3{};
    e3.index = 3; e3.level = AV_LOG_INFO; e3.message = "info"; e3.timestamp = QDateTime::currentDateTime();
    entries << e1 << e2 << e3;
    model.appendEntries(entries);

    // 过滤仅显示 Error 及以上
    proxy.setMinLevel(AV_LOG_ERROR);
    EXPECT_EQ(proxy.rowCount(), 1);

    // 过滤 Warning 及以上
    proxy.setMinLevel(AV_LOG_WARNING);
    EXPECT_EQ(proxy.rowCount(), 2);

    // 显示全部
    proxy.setMinLevel(AV_LOG_TRACE);
    EXPECT_EQ(proxy.rowCount(), 3);
}

// ===== Widget 集成测试 =====

TEST(LogAnalysisWidgetTest, WidgetCreateAndClear) {
    LogAnalysisWidget widget;
    widget.clear(); // 不应崩溃
}

TEST(LogAnalysisWidgetTest, TakePendingEntries) {
    // 安装回调后，pending 队列初始应为空（或已有之前的内容）
    LogAnalysisWidget::installCallback();
    auto entries = LogAnalysisWidget::takePendingEntries();
    // 不做数量断言，只要不崩溃
    Q_UNUSED(entries);
}
