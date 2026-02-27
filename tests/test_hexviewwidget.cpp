#include <gtest/gtest.h>
#include "hexviewwidget.h"

#include <QApplication>

TEST(HexViewWidgetTest, SetDataAndSize) {
    HexViewWidget widget;
    QByteArray data(256, '\x42');
    widget.setData(data);

    EXPECT_EQ(widget.data().size(), 256);
}

TEST(HexViewWidgetTest, BaseOffset) {
    HexViewWidget widget;
    widget.setBaseOffset(0x1000);
    EXPECT_EQ(widget.baseOffset(), 0x1000);
}

TEST(HexViewWidgetTest, EmptyData) {
    HexViewWidget widget;
    widget.setData(QByteArray());

    EXPECT_EQ(widget.data().size(), 0);
    EXPECT_EQ(widget.lineCount(), 0);
}

TEST(HexViewWidgetTest, LargeData) {
    HexViewWidget widget;
    QByteArray data(1024 * 1024, '\x00'); // 1MB
    widget.setData(data);

    EXPECT_EQ(widget.data().size(), 1024 * 1024);
}

TEST(HexViewWidgetTest, LineCount) {
    HexViewWidget widget;

    // 空数据
    widget.setData(QByteArray());
    EXPECT_EQ(widget.lineCount(), 0);

    // 1 字节 → 1 行
    widget.setData(QByteArray(1, '\x00'));
    EXPECT_EQ(widget.lineCount(), 1);

    // 16 字节 → 1 行
    widget.setData(QByteArray(16, '\x00'));
    EXPECT_EQ(widget.lineCount(), 1);

    // 17 字节 → 2 行
    widget.setData(QByteArray(17, '\x00'));
    EXPECT_EQ(widget.lineCount(), 2);

    // 32 字节 → 2 行
    widget.setData(QByteArray(32, '\x00'));
    EXPECT_EQ(widget.lineCount(), 2);

    // 33 字节 → 3 行
    widget.setData(QByteArray(33, '\x00'));
    EXPECT_EQ(widget.lineCount(), 3);

    // 精确计算：(n + 15) / 16
    for (int n = 0; n <= 100; ++n) {
        widget.setData(QByteArray(n, '\x00'));
        int expected = (n == 0) ? 0 : (n + 15) / 16;
        EXPECT_EQ(widget.lineCount(), expected) << "n=" << n;
    }
}
