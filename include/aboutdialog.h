#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>

class QLabel;
class QTextBrowser;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QShowEvent;

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

    // 静态辅助方法（暴露以供单元测试）

    /// 组装完整版本标识，如 "0.1.0-3-gabcdef (Release, Static)"
    static QString buildVersionString();

    /// 组装构建环境信息文本（OS、编译器、Qt、FFmpeg 各子库版本）
    static QString buildInfoText();

    /// 比较两个版本字符串，若 remote > local 返回 true
    static bool isNewerVersion(const QString &remoteTag, const QString &localVersion);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onUpdateCheckFinished(QNetworkReply *reply);
    void onSpinnerTick();

private:
    void setupUI();
    void checkForUpdates();
    void adjustSizeToContent();

    QTextBrowser *m_browser = nullptr;
    QLabel *m_updateIconLabel = nullptr;   // 动画转圈 / ✅ / ⚠️ 图标
    QLabel *m_updateTextLabel = nullptr;   // 状态文字
    QNetworkAccessManager *m_networkManager = nullptr;
    QTimer *m_spinnerTimer = nullptr;
    int m_spinnerAngle = 0;
    bool m_sizeAdjusted = false;
};

#endif // ABOUTDIALOG_H
