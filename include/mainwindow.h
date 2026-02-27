#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class PacketReader;
class PacketListModel;
class PacketFilterProxyModel;
class QTabWidget;
class QTableView;
class QComboBox;
class QLabel;
class QProgressDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openFile();
    void onPacketDoubleClicked(const QModelIndex &index);
    void onTabCloseRequested(int tabIndex);
    void onFilterChanged(int comboIndex);

private:
    void setupUI();
    void setupMenuAndToolbar();
    void updateStatusBar();
    void closeAllDetailTabs();

    PacketReader *m_reader = nullptr;
    PacketListModel *m_model = nullptr;
    PacketFilterProxyModel *m_proxyModel = nullptr;

    QTabWidget *m_tabWidget = nullptr;
    QTableView *m_tableView = nullptr;
    QComboBox *m_filterCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
};

#endif // MAINWINDOW_H
