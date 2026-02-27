#ifndef LOGANALYSISWIDGET_H
#define LOGANALYSISWIDGET_H

#include <QWidget>
#include <QVector>
#include <QMutex>
#include <QDateTime>
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

class QTableView;
class QComboBox;
class QPushButton;
class QTimer;

/// 单条 FFmpeg 日志条目
struct LogEntry {
    int index;            // 序号（从 1 开始）
    int level;            // AV_LOG_XXX 级别
    QString className;    // 来源类名（如 "h264", "aac", "mov"）
    QString message;      // 日志消息（已去末尾换行）
    QDateTime timestamp;  // 捕获时间
};

/// FFmpeg 日志表格模型
class LogTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColType = 0,      // 级别图标
        ColIndex,         // 序号
        ColLevel,         // 级别文字
        ColDescription,   // 消息摘要
        ColDetails,       // 完整信息（含类名+时间戳）
        ColCount
    };

    explicit LogTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    /// 追加日志条目（线程安全由调用方通过 pending 队列保证）
    void appendEntries(const QVector<LogEntry> &entries);

    /// 清空所有日志
    void clearAll();

    /// 获取指定行的日志级别
    int levelAt(int row) const;

private:
    QVector<LogEntry> m_entries;
};

/// 级别过滤代理模型
class LogFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit LogFilterProxyModel(QObject *parent = nullptr);

    /// 设置最低显示级别（AV_LOG_XXX，数值越小越严重）
    void setMinLevel(int level);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    int m_minLevel = 56; // 默认显示所有（AV_LOG_TRACE=56）
};

/// FFmpeg 日志分析面板
class LogAnalysisWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogAnalysisWidget(QWidget *parent = nullptr);
    ~LogAnalysisWidget();

    /// 清空日志
    void clear();

    // ===== 静态方法（公开供测试） =====

    /// AV_LOG_XXX 级别转文字
    static QString levelToString(int avLogLevel);

    /// AV_LOG_XXX 级别转图标字符
    static QString levelToIcon(int avLogLevel);

    /// 安装全局 FFmpeg 日志回调（仅首次调用有效）
    static void installCallback();

    /// 从 pending 队列中取出所有待处理条目
    static QVector<LogEntry> takePendingEntries();

private slots:
    void pollPendingLogs();
    void onLevelFilterChanged(int comboIndex);
    void onClearClicked();

private:
    LogTableModel *m_logModel = nullptr;
    LogFilterProxyModel *m_filterProxy = nullptr;
    QTableView *m_tableView = nullptr;
    QComboBox *m_levelCombo = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QTimer *m_pollTimer = nullptr;

    // ===== 全局 FFmpeg 日志回调的共享数据 =====
    static QMutex s_mutex;
    static QVector<LogEntry> s_pendingEntries;
    static int s_nextIndex;
    static bool s_callbackInstalled;
    static void ffmpegLogCallback(void *ptr, int level, const char *fmt, va_list vl);
};

#endif // LOGANALYSISWIDGET_H
