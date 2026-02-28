#include "aboutdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QFont>
#include <QSysInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QTextBrowser>
#include <QScrollArea>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QApplication>

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// ============================================
// 宏默认值（防止未定义时编译报错）
// ============================================
#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif
#ifndef APP_BUILD_TYPE
#define APP_BUILD_TYPE "unknown"
#endif
#ifndef APP_COMPILER_ID
#define APP_COMPILER_ID "unknown"
#endif
#ifndef APP_COMPILER_VERSION
#define APP_COMPILER_VERSION "unknown"
#endif
#ifndef APP_BUILD_TIMESTAMP
#define APP_BUILD_TIMESTAMP "unknown"
#endif
#ifndef APP_OS_NAME
#define APP_OS_NAME "unknown"
#endif
#ifndef APP_OS_VERSION
#define APP_OS_VERSION "unknown"
#endif
#ifndef APP_STATIC_BUILD
#define APP_STATIC_BUILD 0
#endif

// FFmpeg 版本号宏 → 字符串
static QString ffmpegVersionToString(unsigned int ver)
{
    return QStringLiteral("%1.%2.%3")
        .arg(ver >> 16)
        .arg((ver >> 8) & 0xFF)
        .arg(ver & 0xFF);
}

// 转圈动画帧（Unicode braille spinner）
static const char *kSpinnerFrames[] = {
    "\u280b", "\u2819", "\u2839", "\u2838",
    "\u283c", "\u2834", "\u2826", "\u2827",
    "\u2807", "\u280f"
};
static constexpr int kSpinnerFrameCount = 10;

// ============================================
// 静态辅助方法
// ============================================

QString AboutDialog::buildVersionString()
{
    QString version = QStringLiteral(APP_VERSION);
    QString buildType = QStringLiteral(APP_BUILD_TYPE);
    QString linkType = APP_STATIC_BUILD ? QStringLiteral("Static") : QStringLiteral("Dynamic");

    return QStringLiteral("%1 (%2, %3)").arg(version, buildType, linkType);
}

QString AboutDialog::buildInfoText()
{
    QStringList lines;

    // 操作系统（运行时）
    lines << QStringLiteral("操作系统: %1").arg(QSysInfo::prettyProductName());

    // 编译器（编译期宏）
    lines << QStringLiteral("编译器: %1 %2").arg(
        QStringLiteral(APP_COMPILER_ID),
        QStringLiteral(APP_COMPILER_VERSION));

    // Qt 版本（运行时）
    lines << QStringLiteral("Qt: %1").arg(QString::fromLatin1(qVersion()));

    // FFmpeg 各子库版本（运行时）
    lines << QStringLiteral("libavcodec: %1").arg(ffmpegVersionToString(avcodec_version()));
    lines << QStringLiteral("libavformat: %1").arg(ffmpegVersionToString(avformat_version()));
    lines << QStringLiteral("libavutil: %1").arg(ffmpegVersionToString(avutil_version()));
    lines << QStringLiteral("libswscale: %1").arg(ffmpegVersionToString(swscale_version()));
    lines << QStringLiteral("libswresample: %1").arg(ffmpegVersionToString(swresample_version()));

    return lines.join(QStringLiteral("\n"));
}

bool AboutDialog::isNewerVersion(const QString &remoteTag, const QString &localVersion)
{
    // 去掉 'v' 前缀，去掉 -dirty 等后缀，只取 x.y.z 部分
    auto normalize = [](const QString &ver) -> QVector<int> {
        QString s = ver.trimmed();
        if (s.startsWith(QLatin1Char('v')) || s.startsWith(QLatin1Char('V')))
            s = s.mid(1);
        // 取第一段（到 '-' 为止）
        int dashIdx = s.indexOf(QLatin1Char('-'));
        if (dashIdx >= 0)
            s = s.left(dashIdx);
        QStringList parts = s.split(QLatin1Char('.'));
        QVector<int> nums;
        for (const QString &p : parts) {
            bool ok = false;
            int n = p.toInt(&ok);
            nums.append(ok ? n : 0);
        }
        return nums;
    };

    QVector<int> remote = normalize(remoteTag);
    QVector<int> local = normalize(localVersion);

    int len = qMax(remote.size(), local.size());
    for (int i = 0; i < len; ++i) {
        int r = (i < remote.size()) ? remote[i] : 0;
        int l = (i < local.size()) ? local[i] : 0;
        if (r > l) return true;
        if (r < l) return false;
    }
    return false;
}

// ============================================
// 构造函数 & UI
// ============================================

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("关于 VideoAnalyser"));
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumSize(520, 560);
    resize(520, 560);

    m_networkManager = new QNetworkAccessManager(this);

    // spinner 动画定时器
    m_spinnerTimer = new QTimer(this);
    m_spinnerTimer->setInterval(80);
    connect(m_spinnerTimer, &QTimer::timeout, this, &AboutDialog::onSpinnerTick);

    setupUI();
    checkForUpdates();
}

// 生成信息表格 HTML（key-value 行，带合适间距）
static QString buildInfoTableHtml(const QVector<QPair<QString, QString>> &rows)
{
    QString html = QStringLiteral(
        "<table cellspacing='0' cellpadding='3' style='margin: 4px 0;'>");
    for (const auto &row : rows) {
        html += QStringLiteral(
            "<tr>"
            "<td style='padding: 3px 12px 3px 0; white-space: nowrap;'><b>%1</b></td>"
            "<td style='padding: 3px 0;'>%2</td>"
            "</tr>")
            .arg(row.first, row.second);
    }
    html += QStringLiteral("</table>");
    return html;
}

void AboutDialog::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 12);

    // 使用 QTextBrowser 作为主体内容区域（支持富文本 + 鼠标选中复制）
    auto *browser = new QTextBrowser();
    browser->setOpenExternalLinks(true);
    browser->setFrameShape(QFrame::NoFrame);
    browser->setReadOnly(true);
    browser->setStyleSheet(QStringLiteral(
        "QTextBrowser { background: transparent; selection-background-color: palette(highlight); }"));

    // ---- 组装 HTML ----
    QString buildType = QStringLiteral(APP_BUILD_TYPE);
    QString linkType = APP_STATIC_BUILD ? QStringLiteral("Static") : QStringLiteral("Dynamic");

    // 版本信息表格
    QString versionTable = buildInfoTableHtml({
        {QStringLiteral("版本号"), QStringLiteral(APP_VERSION)},
        {QStringLiteral("构建类型"), QStringLiteral("%1 | %2").arg(buildType, linkType)},
        {QStringLiteral("构建时间"), QStringLiteral(APP_BUILD_TIMESTAMP)},
    });

    // 构建环境表格
    QString envTable = buildInfoTableHtml({
        {QStringLiteral("操作系统"), QSysInfo::prettyProductName()},
        {QStringLiteral("编译器"),
         QStringLiteral("%1 %2").arg(QStringLiteral(APP_COMPILER_ID),
                                     QStringLiteral(APP_COMPILER_VERSION))},
        {QStringLiteral("Qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("libavcodec"), ffmpegVersionToString(avcodec_version())},
        {QStringLiteral("libavformat"), ffmpegVersionToString(avformat_version())},
        {QStringLiteral("libavutil"), ffmpegVersionToString(avutil_version())},
        {QStringLiteral("libswscale"), ffmpegVersionToString(swscale_version())},
        {QStringLiteral("libswresample"), ffmpegVersionToString(swresample_version())},
    });

    QString html = QStringLiteral(
        "<div style='text-align: center; padding: 16px 0 4px 0;'>"
        "  <p style='font-size: 22px; font-weight: bold; margin: 0;'>VideoAnalyser</p>"
        "  <p style='color: gray; margin: 4px 0;'>基于 Qt + FFmpeg 的视频 Packet 分析工具</p>"
        "  <p style='margin: 2px 0;'>作者: gaoxin</p>"
        "</div>"
        "<hr style='border: none; border-top: 1px solid palette(mid); margin: 8px 20px;' />"

        "<div style='margin: 0 20px;'>"
        "  <p style='font-size: 13px; font-weight: bold; margin: 8px 0 4px 0;'>版本信息</p>"
        "  %1"
        "</div>"

        "<hr style='border: none; border-top: 1px solid palette(mid); margin: 8px 20px;' />"

        "<div style='margin: 0 20px;'>"
        "  <p style='font-size: 13px; font-weight: bold; margin: 8px 0 4px 0;'>构建环境</p>"
        "  %2"
        "</div>"

        "<hr style='border: none; border-top: 1px solid palette(mid); margin: 8px 20px;' />"

        "<div style='margin: 4px 20px 0 20px;'>"
        "  <p style='margin: 4px 0;'>🔗 <a href=\"https://github.com/GX-Swjtu/VideoAnalyser\">"
        "GitHub: GX-Swjtu/VideoAnalyser</a></p>"
        "</div>"
    ).arg(versionTable, envTable);

    browser->setHtml(html);
    mainLayout->addWidget(browser, 1);

    // ---- 更新检查状态行（在 browser 外部，独立 widget） ----
    auto *updateRow = new QHBoxLayout();
    updateRow->setContentsMargins(24, 4, 24, 4);

    m_updateIconLabel = new QLabel();
    m_updateIconLabel->setFixedWidth(20);
    m_updateIconLabel->setAlignment(Qt::AlignCenter);
    updateRow->addWidget(m_updateIconLabel);

    m_updateTextLabel = new QLabel(QStringLiteral("正在检查更新..."));
    m_updateTextLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_updateTextLabel->setOpenExternalLinks(true);
    updateRow->addWidget(m_updateTextLabel, 1);

    mainLayout->addLayout(updateRow);

    mainLayout->addSpacing(4);

    // ---- 底部按钮行 ----
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(24, 0, 24, 0);

    auto *copyBtn = new QPushButton(QStringLiteral("复制信息"));
    copyBtn->setToolTip(QStringLiteral("将全部版本和构建信息复制到剪贴板"));
    connect(copyBtn, &QPushButton::clicked, this, [this, copyBtn]() {
        QString text;
        text += QStringLiteral("VideoAnalyser %1\n").arg(buildVersionString());
        text += QStringLiteral("构建时间: %1\n\n").arg(QStringLiteral(APP_BUILD_TIMESTAMP));
        text += buildInfoText();
        text += QStringLiteral("\n\nhttps://github.com/GX-Swjtu/VideoAnalyser");
        QApplication::clipboard()->setText(text);
        copyBtn->setText(QStringLiteral("已复制 ✓"));
        QTimer::singleShot(1500, copyBtn, [copyBtn]() {
            copyBtn->setText(QStringLiteral("复制信息"));
        });
    });
    btnLayout->addWidget(copyBtn);

    btnLayout->addStretch();

    auto *okBtn = new QPushButton(QStringLiteral("确定"));
    okBtn->setDefault(true);
    okBtn->setMinimumWidth(80);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(okBtn);
    mainLayout->addLayout(btnLayout);
}

// ============================================
// 更新检查动画
// ============================================

void AboutDialog::onSpinnerTick()
{
    m_spinnerAngle = (m_spinnerAngle + 1) % kSpinnerFrameCount;
    m_updateIconLabel->setText(QString::fromUtf8(kSpinnerFrames[m_spinnerAngle]));
}

// ============================================
// 更新检查
// ============================================

void AboutDialog::checkForUpdates()
{
    // 启动 spinner 动画
    m_updateIconLabel->setText(QString::fromUtf8(kSpinnerFrames[0]));
    m_spinnerTimer->start();

    QUrl url(QStringLiteral("https://api.github.com/repos/GX-Swjtu/VideoAnalyser/releases/latest"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VideoAnalyser/%1").arg(QStringLiteral(APP_VERSION)));
    request.setRawHeader("Accept", "application/vnd.github.v3+json");

    QNetworkReply *reply = m_networkManager->get(request);

    // 超时 8 秒自动放弃
    QTimer::singleShot(8000, reply, &QNetworkReply::abort);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onUpdateCheckFinished(reply);
    });
}

void AboutDialog::onUpdateCheckFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    m_spinnerTimer->stop();

    // 任何错误（含超时 abort）→ 显示失败提示
    if (reply->error() != QNetworkReply::NoError) {
        m_updateIconLabel->setText(QStringLiteral("❌"));
        m_updateTextLabel->setStyleSheet(QStringLiteral("color: gray;"));
        m_updateTextLabel->setText(QStringLiteral("检查新版本失败"));
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        m_updateIconLabel->setText(QStringLiteral("❌"));
        m_updateTextLabel->setStyleSheet(QStringLiteral("color: gray;"));
        m_updateTextLabel->setText(QStringLiteral("检查新版本失败"));
        return;
    }

    QJsonObject obj = doc.object();
    QString remoteTag = obj.value(QStringLiteral("tag_name")).toString();
    if (remoteTag.isEmpty()) {
        m_updateIconLabel->setText(QStringLiteral("❌"));
        m_updateTextLabel->setStyleSheet(QStringLiteral("color: gray;"));
        m_updateTextLabel->setText(QStringLiteral("检查新版本失败"));
        return;
    }

    QString localVersion = QStringLiteral(APP_VERSION);

    if (isNewerVersion(remoteTag, localVersion)) {
        m_updateIconLabel->setText(QStringLiteral("⚠️"));
        m_updateTextLabel->setStyleSheet(QString());
        m_updateTextLabel->setText(
            QStringLiteral("发现新版本 <b>%1</b> — "
                          "<a href=\"https://github.com/GX-Swjtu/VideoAnalyser/releases/latest\">前往下载</a>")
                .arg(remoteTag));
    } else {
        m_updateIconLabel->setText(QStringLiteral("✅"));
        m_updateTextLabel->setStyleSheet(QStringLiteral("color: green;"));
        m_updateTextLabel->setText(QStringLiteral("已是最新版本"));
    }
}
