#ifndef THEMEUTILS_H
#define THEMEUTILS_H

#include <QApplication>
#include <QStyleHints>

/// 全局主题工具：检测当前是否处于暗色模式
/// 使用 Qt 6.5+ QStyleHints::colorScheme() API
inline bool isDarkMode()
{
    return QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

#endif // THEMEUTILS_H
