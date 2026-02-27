#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMetaObject>

class PacketReader;
class PacketListModel;
class PacketFilterProxyModel;
class QTabWidget;
class QTableView;
class QComboBox;
class QLabel;
class QProgressDialog;
class MediaInfoWidget;
class TimestampChartWidget;
class BitrateChartWidget;
class AVSyncChartWidget;
class LogAnalysisWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void openFile();
    void onPacketDoubleClicked(const QModelIndex &index);
    void onFilterChanged(int comboIndex);

private:
    void setupUI();
    void setupMenuAndToolbar();
    void loadFile(const QString &filePath);
    void updateStatusBar();
    void clearAllAnalysis();

    PacketReader *m_reader = nullptr;
    PacketListModel *m_model = nullptr;
    PacketFilterProxyModel *m_proxyModel = nullptr;

    QTabWidget *m_tabWidget = nullptr;
    QTableView *m_tableView = nullptr;
    QComboBox *m_filterCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QMetaObject::Connection m_progressConnection;

    // 分析标签页
    MediaInfoWidget *m_mediaInfoWidget = nullptr;
    TimestampChartWidget *m_timestampChartWidget = nullptr;
    BitrateChartWidget *m_bitrateChartWidget = nullptr;
    AVSyncChartWidget *m_avsyncChartWidget = nullptr;
    LogAnalysisWidget *m_logAnalysisWidget = nullptr;
};

#endif // MAINWINDOW_H
