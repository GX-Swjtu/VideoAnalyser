#include "mainwindow.h"
#include "packetreader.h"
#include "packetlistmodel.h"
#include "packetdetailwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTableView>
#include <QHeaderView>
#include <QComboBox>
#include <QLabel>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QProgressDialog>
#include <QMessageBox>
#include <QApplication>
#include <QTime>

extern "C" {
#include <libavutil/avutil.h>
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_reader = new PacketReader(this);
    m_model = new PacketListModel(this);
    m_proxyModel = new PacketFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);

    setupUI();
    setupMenuAndToolbar();

    setWindowTitle(QStringLiteral("VideoAnalyser"));
    resize(1200, 800);
}

MainWindow::~MainWindow()
{
    m_reader->close();
}

void MainWindow::setupUI()
{
    // 中央 TabWidget
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);

    // Packet 列表作为第一个 tab
    auto *listWidget = new QWidget();
    auto *listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(0, 0, 0, 0);

    m_tableView = new QTableView();
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(true);
    m_tableView->sortByColumn(PacketListModel::ColPTS, Qt::AscendingOrder);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->verticalHeader()->setDefaultSectionSize(24);
    m_tableView->verticalHeader()->hide();
    m_tableView->setShowGrid(false);

    connect(m_tableView, &QTableView::doubleClicked, this, &MainWindow::onPacketDoubleClicked);

    listLayout->addWidget(m_tableView);

    m_tabWidget->addTab(listWidget, QStringLiteral("Packet 列表"));

    setCentralWidget(m_tabWidget);

    // 状态栏
    m_statusLabel = new QLabel(QStringLiteral("就绪"));
    statusBar()->addWidget(m_statusLabel, 1);
}

void MainWindow::setupMenuAndToolbar()
{
    // 菜单栏
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    auto *openAction = fileMenu->addAction(QStringLiteral("打开(&O)..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

    // 工具栏
    auto *toolbar = addToolBar(QStringLiteral("工具栏"));
    toolbar->setMovable(false);

    auto *openBtn = toolbar->addAction(QStringLiteral("\U0001F4C2 打开"));
    connect(openBtn, &QAction::triggered, this, &MainWindow::openFile);

    toolbar->addSeparator();

    toolbar->addWidget(new QLabel(QStringLiteral(" 筛选: ")));
    m_filterCombo = new QComboBox();
    m_filterCombo->addItem(QStringLiteral("全部"), -1);
    m_filterCombo->addItem(QStringLiteral("仅视频"), static_cast<int>(AVMEDIA_TYPE_VIDEO));
    m_filterCombo->addItem(QStringLiteral("仅音频"), static_cast<int>(AVMEDIA_TYPE_AUDIO));
    m_filterCombo->addItem(QStringLiteral("仅字幕"), static_cast<int>(AVMEDIA_TYPE_SUBTITLE));
    m_filterCombo->addItem(QStringLiteral("仅数据"), static_cast<int>(AVMEDIA_TYPE_DATA));
    m_filterCombo->setMinimumWidth(100);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onFilterChanged);
    toolbar->addWidget(m_filterCombo);
}

void MainWindow::openFile()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("打开视频文件"),
        QString(),
        QStringLiteral("视频文件 (*.mp4 *.mkv *.flv *.avi *.ts *.mov *.wmv *.webm);;所有文件 (*)")
    );

    if (filePath.isEmpty()) return;

    // 关闭之前的
    closeAllDetailTabs();
    m_model->clear();
    m_reader->close();

    // 打开新文件
    if (!m_reader->open(filePath)) {
        QMessageBox::critical(this, QStringLiteral("错误"),
                              QStringLiteral("无法打开文件:\n%1").arg(filePath));
        return;
    }

    // 进度对话框
    QProgressDialog progress(QStringLiteral("正在读取 Packet..."), QStringLiteral("取消"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);

    connect(m_reader, &PacketReader::progressChanged, &progress, [&progress](int current, int total) {
        if (total > 0) {
            progress.setMaximum(total);
            progress.setValue(current);
        }
    });

    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = m_reader->readAllPackets();
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("警告"),
                             QStringLiteral("读取 Packet 时出错"));
    }

    m_model->setPackets(m_reader->packets());

    // 调整列宽
    m_tableView->resizeColumnsToContents();

    // 重置筛选
    m_filterCombo->setCurrentIndex(0);

    // 更新 UI
    updateStatusBar();

    QFileInfo fi(filePath);
    setWindowTitle(QStringLiteral("VideoAnalyser - %1").arg(fi.fileName()));
}

void MainWindow::onPacketDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;

    // 映射回源模型
    QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    int packetIndex = sourceIndex.row();

    if (packetIndex < 0 || packetIndex >= m_reader->packetCount())
        return;

    const PacketInfo &pkt = m_reader->packetAt(packetIndex);

    // 创建独立弹出窗口（parent=this，主窗口关闭时自动销毁）
    auto *detail = new PacketDetailWidget(m_reader, packetIndex, this);

    QString title = QStringLiteral("Packet #%1 (%2)")
                        .arg(pkt.index)
                        .arg(PacketListModel::mediaTypeString(pkt.mediaType));

    detail->setWindowTitle(title);
    detail->setWindowFlags(Qt::Window);
    detail->setAttribute(Qt::WA_DeleteOnClose);
    detail->resize(900, 600);
    detail->show();
}

void MainWindow::onTabCloseRequested(int tabIndex)
{
    // 第一个 tab (Packet 列表) 不可关闭
    if (tabIndex == 0) return;

    QWidget *w = m_tabWidget->widget(tabIndex);
    m_tabWidget->removeTab(tabIndex);
    delete w;
}

void MainWindow::onFilterChanged(int comboIndex)
{
    int typeValue = m_filterCombo->itemData(comboIndex).toInt();
    m_proxyModel->setMediaTypeFilter(typeValue);
}

void MainWindow::updateStatusBar()
{
    if (!m_reader->isOpen()) {
        m_statusLabel->setText(QStringLiteral("就绪"));
        return;
    }

    double dur = m_reader->durationSeconds();
    int ms = static_cast<int>(dur * 1000.0);
    QString durStr = QTime::fromMSecsSinceStartOfDay(ms).toString(QStringLiteral("HH:mm:ss"));

    m_statusLabel->setText(QStringLiteral("%1 | 时长: %2 | %3 streams | %4 packets")
                               .arg(m_reader->formatName())
                               .arg(durStr)
                               .arg(m_reader->streams().size())
                               .arg(m_reader->packetCount()));
}

void MainWindow::closeAllDetailTabs()
{
    // 从后往前删除，保留第 0 个 tab
    while (m_tabWidget->count() > 1) {
        QWidget *w = m_tabWidget->widget(m_tabWidget->count() - 1);
        m_tabWidget->removeTab(m_tabWidget->count() - 1);
        delete w;
    }
}
