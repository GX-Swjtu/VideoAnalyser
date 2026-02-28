#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 设置应用程序图标（所有窗口继承）
    QIcon appIcon;
    appIcon.addFile(QStringLiteral(":/icons/app_16"), QSize(16, 16));
    appIcon.addFile(QStringLiteral(":/icons/app_32"), QSize(32, 32));
    appIcon.addFile(QStringLiteral(":/icons/app_48"), QSize(48, 48));
    appIcon.addFile(QStringLiteral(":/icons/app_256"), QSize(256, 256));
    a.setWindowIcon(appIcon);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "VideoAnalyser_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    w.show();
    return a.exec();
}
