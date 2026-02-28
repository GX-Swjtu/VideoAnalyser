#include <gtest/gtest.h>
#include "aboutdialog.h"
#include <QString>
#include <QApplication>

// ============================================
// buildVersionString 测试
// ============================================

TEST(AboutDialogTest, BuildVersionStringNotEmpty)
{
    QString ver = AboutDialog::buildVersionString();
    EXPECT_FALSE(ver.isEmpty());
}

TEST(AboutDialogTest, BuildVersionStringContainsBuildType)
{
    QString ver = AboutDialog::buildVersionString();
    // 应包含 Release 或 Debug
    bool hasType = ver.contains("Release") || ver.contains("Debug");
    EXPECT_TRUE(hasType) << "版本字符串应包含构建类型: " << ver.toStdString();
}

TEST(AboutDialogTest, BuildVersionStringContainsLinkType)
{
    QString ver = AboutDialog::buildVersionString();
    // 应包含 Static 或 Dynamic
    bool hasLink = ver.contains("Static") || ver.contains("Dynamic");
    EXPECT_TRUE(hasLink) << "版本字符串应包含链接类型: " << ver.toStdString();
}

// ============================================
// buildInfoText 测试
// ============================================

TEST(AboutDialogTest, BuildInfoContainsQtVersion)
{
    QString info = AboutDialog::buildInfoText();
    // 应包含 qVersion() 返回的运行时 Qt 版本
    EXPECT_TRUE(info.contains(QString::fromLatin1(qVersion())))
        << "构建信息应包含 Qt 版本: " << info.toStdString();
}

TEST(AboutDialogTest, BuildInfoContainsFFmpegLibs)
{
    QString info = AboutDialog::buildInfoText();
    // 应包含实际使用的 FFmpeg 子库
    EXPECT_TRUE(info.contains("libavcodec"))
        << "构建信息应包含 libavcodec: " << info.toStdString();
    EXPECT_TRUE(info.contains("libavformat"))
        << "构建信息应包含 libavformat: " << info.toStdString();
}

TEST(AboutDialogTest, BuildInfoContainsCompiler)
{
    QString info = AboutDialog::buildInfoText();
    EXPECT_TRUE(info.contains(QStringLiteral("编译器")))
        << "构建信息应包含编译器字段: " << info.toStdString();
}

TEST(AboutDialogTest, BuildInfoContainsOS)
{
    QString info = AboutDialog::buildInfoText();
    EXPECT_TRUE(info.contains(QStringLiteral("操作系统")))
        << "构建信息应包含操作系统字段: " << info.toStdString();
}

TEST(AboutDialogTest, BuildInfoContainsSubLibVersions)
{
    QString info = AboutDialog::buildInfoText();
    EXPECT_TRUE(info.contains("libavcodec"));
    EXPECT_TRUE(info.contains("libavformat"));
    EXPECT_TRUE(info.contains("libswscale"));
    EXPECT_TRUE(info.contains("libswresample"));
}

// ============================================
// isNewerVersion 测试
// ============================================

TEST(AboutDialogTest, IsNewerVersion_NewerMajor)
{
    EXPECT_TRUE(AboutDialog::isNewerVersion("v2.0.0", "1.0.0"));
}

TEST(AboutDialogTest, IsNewerVersion_NewerMinor)
{
    EXPECT_TRUE(AboutDialog::isNewerVersion("v0.2.0", "0.1.0"));
}

TEST(AboutDialogTest, IsNewerVersion_NewerPatch)
{
    EXPECT_TRUE(AboutDialog::isNewerVersion("v0.1.1", "0.1.0"));
}

TEST(AboutDialogTest, IsNewerVersion_SameVersion)
{
    EXPECT_FALSE(AboutDialog::isNewerVersion("v0.1.0", "0.1.0"));
}

TEST(AboutDialogTest, IsNewerVersion_OlderVersion)
{
    EXPECT_FALSE(AboutDialog::isNewerVersion("v0.1.0", "0.2.0"));
}

TEST(AboutDialogTest, IsNewerVersion_WithDirtySuffix)
{
    // 本地版本带 -dirty 后缀时应正确忽略
    EXPECT_TRUE(AboutDialog::isNewerVersion("v1.0.0", "0.1.0-3-gabcdef-dirty"));
}

TEST(AboutDialogTest, IsNewerVersion_TagWithGitDescribe)
{
    // v0.2.0 > 0.1.0-5-g1234567
    EXPECT_TRUE(AboutDialog::isNewerVersion("v0.2.0", "0.1.0-5-g1234567"));
}

TEST(AboutDialogTest, IsNewerVersion_TwoPartVersion)
{
    // 支持 x.y 格式（无 patch）
    EXPECT_TRUE(AboutDialog::isNewerVersion("v0.2", "0.1"));
    EXPECT_FALSE(AboutDialog::isNewerVersion("v0.1", "0.1"));
}

// ============================================
// 对话框创建测试
// ============================================

TEST(AboutDialogTest, DialogCreation)
{
    // 构造不崩溃（会触发网络请求，测试环境下会超时/失败但静默处理）
    AboutDialog dlg;
    EXPECT_TRUE(dlg.windowTitle().contains(QStringLiteral("关于")));
    EXPECT_GE(dlg.width(), 520);
}
