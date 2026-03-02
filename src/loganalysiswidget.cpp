#include "loganalysiswidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QHeaderView>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QBrush>
#include <QFont>
#include <QApplication>
#include <QClipboard>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QShortcut>

#include <algorithm>

extern "C" {
#include <libavutil/log.h>
}

// ===== 静态成员初始化 =====
QMutex LogAnalysisWidget::s_mutex;
QVector<LogEntry> LogAnalysisWidget::s_pendingEntries;
int LogAnalysisWidget::s_nextIndex = 1;
int LogAnalysisWidget::s_captureLevel = AV_LOG_WARNING;
bool LogAnalysisWidget::s_callbackInstalled = false;

// ===== LogTableModel =====

LogTableModel::LogTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int LogTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

int LogTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant LogTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};

    const auto &entry = m_entries[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColType:
            return LogAnalysisWidget::levelToIcon(entry.level);
        case ColIndex:
            return entry.index;
        case ColLevel:
            return LogAnalysisWidget::levelToString(entry.level);
        case ColDescription:
            return entry.message;
        case ColDetails:
            return QStringLiteral("[%1] %2: %3")
                .arg(entry.timestamp.toString(QStringLiteral("yyyy-M-d HH:mm:ss")))
                .arg(entry.className)
                .arg(entry.message);
        default: return {};
        }
    }

    if (role == Qt::ForegroundRole) {
        if (entry.level <= AV_LOG_FATAL) return QBrush(Qt::red);
        if (entry.level <= AV_LOG_ERROR) return QBrush(QColor(200, 0, 0));
        if (entry.level <= AV_LOG_WARNING) return QBrush(QColor(180, 120, 0));
        return {};
    }

    return {};
}

QVariant LogTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColType: return QStringLiteral("Type");
    case ColIndex: return QStringLiteral("Index");
    case ColLevel: return QStringLiteral("Level");
    case ColDescription: return QStringLiteral("Description");
    case ColDetails: return QStringLiteral("Details");
    default: return {};
    }
}

void LogTableModel::appendEntries(const QVector<LogEntry> &entries)
{
    if (entries.isEmpty()) return;
    beginInsertRows(QModelIndex(), m_entries.size(), m_entries.size() + entries.size() - 1);
    m_entries.append(entries);
    endInsertRows();
}

void LogTableModel::clearAll()
{
    if (m_entries.isEmpty()) return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

int LogTableModel::levelAt(int row) const
{
    if (row < 0 || row >= m_entries.size()) return AV_LOG_QUIET;
    return m_entries[row].level;
}

const LogEntry &LogTableModel::entryAt(int row) const
{
    static const LogEntry empty{};
    if (row < 0 || row >= m_entries.size()) return empty;
    return m_entries[row];
}

// ===== LogFilterProxyModel =====

LogFilterProxyModel::LogFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

void LogFilterProxyModel::setMinLevel(int level)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    m_minLevel = level;
    endFilterChange();
#else
    m_minLevel = level;
    invalidateFilter();
#endif
}

bool LogFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent);
    auto *model = qobject_cast<LogTableModel *>(sourceModel());
    if (!model) return true;
    int level = model->levelAt(sourceRow);
    // AV_LOG 级别：数值越小越严重（FATAL=8, ERROR=16, WARNING=24, INFO=32, VERBOSE=40, DEBUG=48, TRACE=56）
    return level <= m_minLevel;
}

// ===== LogAnalysisWidget =====

LogAnalysisWidget::LogAnalysisWidget(QWidget *parent)
    : QWidget(parent)
{
    installCallback();

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // 工具栏
    auto *toolbar = new QHBoxLayout();
    toolbar->addWidget(new QLabel(QStringLiteral("记录等级:")));
    m_captureLevelCombo = new QComboBox();
    m_captureLevelCombo->addItem(QStringLiteral("Error"),   AV_LOG_ERROR);
    m_captureLevelCombo->addItem(QStringLiteral("Warning"), AV_LOG_WARNING);
    m_captureLevelCombo->addItem(QStringLiteral("Info"),    AV_LOG_INFO);
    m_captureLevelCombo->addItem(QStringLiteral("Verbose"), AV_LOG_VERBOSE);
    m_captureLevelCombo->addItem(QStringLiteral("Debug"),   AV_LOG_DEBUG);
    m_captureLevelCombo->setCurrentIndex(1); // 默认记录 Warning 及以上
    toolbar->addWidget(m_captureLevelCombo);

    toolbar->addSpacing(12);
    toolbar->addWidget(new QLabel(QStringLiteral("显示过滤:")));
    m_levelCombo = new QComboBox();
    m_levelCombo->addItem(QStringLiteral("Debug"),   AV_LOG_DEBUG);
    m_levelCombo->addItem(QStringLiteral("Verbose"), AV_LOG_VERBOSE);
    m_levelCombo->addItem(QStringLiteral("Info"),    AV_LOG_INFO);
    m_levelCombo->addItem(QStringLiteral("Warning"), AV_LOG_WARNING);
    m_levelCombo->addItem(QStringLiteral("Error"),   AV_LOG_ERROR);
    m_levelCombo->addItem(QStringLiteral("Fatal"),   AV_LOG_FATAL);
    m_levelCombo->setCurrentIndex(m_levelCombo->findData(AV_LOG_WARNING));
    toolbar->addWidget(m_levelCombo);

    toolbar->addSpacing(20);
    m_clearBtn = new QPushButton(QStringLiteral("清空"));
    toolbar->addWidget(m_clearBtn);
    toolbar->addStretch();
    mainLayout->addLayout(toolbar);

    // 模型
    m_logModel = new LogTableModel(this);
    m_filterProxy = new LogFilterProxyModel(this);
    m_filterProxy->setSourceModel(m_logModel);

    // 表格
    m_tableView = new QTableView(this);
    m_tableView->setModel(m_filterProxy);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(false);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->verticalHeader()->setDefaultSectionSize(22);
    m_tableView->verticalHeader()->hide();
    m_tableView->setShowGrid(false);
    // 设置列宽
    m_tableView->setColumnWidth(LogTableModel::ColType, 40);
    m_tableView->setColumnWidth(LogTableModel::ColIndex, 60);
    m_tableView->setColumnWidth(LogTableModel::ColLevel, 70);
    m_tableView->setColumnWidth(LogTableModel::ColDescription, 400);

    mainLayout->addWidget(m_tableView, 1);

    // 信号
    connect(m_captureLevelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogAnalysisWidget::onCaptureLevelChanged);
    connect(m_levelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogAnalysisWidget::onLevelFilterChanged);
    connect(m_clearBtn, &QPushButton::clicked, this, &LogAnalysisWidget::onClearClicked);

    // 定时拉取
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(200);
    connect(m_pollTimer, &QTimer::timeout, this, &LogAnalysisWidget::pollPendingLogs);
    m_pollTimer->start();

    auto *copyShortcut = new QShortcut(QKeySequence::Copy, this);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated, this, [this]() {
        copySelectionToClipboard();
    });

    setCaptureLevel(AV_LOG_WARNING);
    onLevelFilterChanged(m_levelCombo->currentIndex());
}

LogAnalysisWidget::~LogAnalysisWidget()
{
    m_pollTimer->stop();
}

void LogAnalysisWidget::clear()
{
    m_logModel->clearAll();
    // 也清空 pending 队列
    QMutexLocker lock(&s_mutex);
    s_pendingEntries.clear();
}

void LogAnalysisWidget::pollPendingLogs()
{
    auto entries = takePendingEntries();
    if (!entries.isEmpty()) {
        m_logModel->appendEntries(entries);
        // 自动滚动到底部
        m_tableView->scrollToBottom();
    }
}

void LogAnalysisWidget::onLevelFilterChanged(int comboIndex)
{
    int level = m_levelCombo->itemData(comboIndex).toInt();
    m_filterProxy->setMinLevel(level);
}

void LogAnalysisWidget::onCaptureLevelChanged(int comboIndex)
{
    int level = m_captureLevelCombo->itemData(comboIndex).toInt();
    setCaptureLevel(level);
}

void LogAnalysisWidget::onClearClicked()
{
    clear();
}

void LogAnalysisWidget::keyPressEvent(QKeyEvent *event)
{
    if (event && event->matches(QKeySequence::Copy) && copySelectionToClipboard()) {
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

bool LogAnalysisWidget::copySelectionToClipboard()
{
    if (!m_tableView || !m_tableView->selectionModel()) {
        return false;
    }

    QModelIndexList selectedRows = m_tableView->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) {
        return false;
    }

    std::sort(selectedRows.begin(), selectedRows.end(), [](const QModelIndex &lhs, const QModelIndex &rhs) {
        if (lhs.row() != rhs.row()) return lhs.row() < rhs.row();
        return lhs.column() < rhs.column();
    });

    QStringList lines;
    lines << QStringLiteral("Type\tIndex\tLevel\tDescription\tDetails");
    for (const QModelIndex &proxyIndex : selectedRows) {
        QModelIndex sourceIndex = m_filterProxy->mapToSource(proxyIndex);
        const LogEntry &entry = m_logModel->entryAt(sourceIndex.row());
        lines << formatRowTsv(entry);
    }

    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    return true;
}

// ===== 静态方法 =====

QString LogAnalysisWidget::levelToString(int avLogLevel)
{
    if (avLogLevel <= AV_LOG_FATAL)   return QStringLiteral("fatal");
    if (avLogLevel <= AV_LOG_ERROR)   return QStringLiteral("error");
    if (avLogLevel <= AV_LOG_WARNING) return QStringLiteral("warning");
    if (avLogLevel <= AV_LOG_INFO)    return QStringLiteral("info");
    if (avLogLevel <= AV_LOG_VERBOSE) return QStringLiteral("verbose");
    if (avLogLevel <= AV_LOG_DEBUG)   return QStringLiteral("debug");
    return QStringLiteral("trace");
}

QString LogAnalysisWidget::levelToIcon(int avLogLevel)
{
    if (avLogLevel <= AV_LOG_FATAL)   return QStringLiteral("\u2716"); // ✖
    if (avLogLevel <= AV_LOG_ERROR)   return QStringLiteral("\u2716"); // ✖
    if (avLogLevel <= AV_LOG_WARNING) return QStringLiteral("\u26A0"); // ⚠
    if (avLogLevel <= AV_LOG_INFO)    return QStringLiteral("\u2139"); // ℹ
    return QStringLiteral("\u2022"); // •
}

void LogAnalysisWidget::setCaptureLevel(int level)
{
    {
        QMutexLocker lock(&s_mutex);
        s_captureLevel = level;
    }
    av_log_set_level(level);
}

int LogAnalysisWidget::captureLevel()
{
    QMutexLocker lock(&s_mutex);
    return s_captureLevel;
}

void LogAnalysisWidget::installCallback()
{
    if (s_callbackInstalled) return;
    s_callbackInstalled = true;
    av_log_set_level(AV_LOG_WARNING);
    av_log_set_callback(ffmpegLogCallback);
}

QVector<LogEntry> LogAnalysisWidget::takePendingEntries()
{
    QMutexLocker lock(&s_mutex);
    QVector<LogEntry> entries;
    entries.swap(s_pendingEntries);
    return entries;
}

void LogAnalysisWidget::ffmpegLogCallback(void *ptr, int level, const char *fmt, va_list vl)
{
    int currentCaptureLevel = AV_LOG_WARNING;
    {
        QMutexLocker lock(&s_mutex);
        currentCaptureLevel = s_captureLevel;
    }
    // 仅捕获当前设定记录等级及以上（数值越小越严重）
    if (level > currentCaptureLevel) return;

    // 格式化消息
    char line[1024];
    int printPrefix = 1;
    av_log_format_line(ptr, level, fmt, vl, line, sizeof(line), &printPrefix);

    // 提取类名
    QString className;
    if (ptr) {
        AVClass *avc = *reinterpret_cast<AVClass **>(ptr);
        if (avc && avc->class_name)
            className = QString::fromUtf8(avc->class_name);
    }

    QString message = QString::fromUtf8(line).trimmed();
    if (message.isEmpty()) return;

    LogEntry entry;
    entry.level = level;
    entry.className = className;
    entry.message = message;
    entry.timestamp = QDateTime::currentDateTime();

    QMutexLocker lock(&s_mutex);
    entry.index = s_nextIndex++;
    s_pendingEntries.append(entry);
}

QString LogAnalysisWidget::formatRowTsv(const LogEntry &entry)
{
    auto sanitize = [](QString value) {
        value.replace(QLatin1Char('\t'), QLatin1Char(' '));
        value.replace(QLatin1Char('\r'), QLatin1Char(' '));
        value.replace(QLatin1Char('\n'), QLatin1Char(' '));
        return value;
    };

    const QString timestamp = entry.timestamp.toString(QStringLiteral("yyyy-M-d HH:mm:ss"));
    const QString className = entry.className.isEmpty() ? QStringLiteral("-") : entry.className;
    const QString details = QStringLiteral("[%1] %2: %3")
                                .arg(timestamp)
                                .arg(className)
                                .arg(entry.message);

    return QStringLiteral("%1\t%2\t%3\t%4\t%5")
        .arg(sanitize(levelToIcon(entry.level)))
        .arg(entry.index)
        .arg(sanitize(levelToString(entry.level)))
        .arg(sanitize(entry.message))
        .arg(sanitize(details));
}
