#include "hexviewwidget.h"

#include <QPainter>
#include <QScrollBar>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPaintEvent>

HexViewWidget::HexViewWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_font.setPointSize(10);
    setFont(m_font);

    QFontMetrics fm(m_font);
    m_charWidth = fm.horizontalAdvance(QChar('0'));
    m_lineHeight = fm.height() + 2;

    setMinimumWidth(400);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void HexViewWidget::setData(const QByteArray &data)
{
    m_data = data;
    updateScrollBar();
    viewport()->update();
}

void HexViewWidget::setBaseOffset(int64_t offset)
{
    m_baseOffset = offset;
    viewport()->update();
}

int HexViewWidget::lineCount() const
{
    if (m_data.isEmpty()) return 0;
    return (m_data.size() + m_bytesPerLine - 1) / m_bytesPerLine;
}

int HexViewWidget::visibleLines() const
{
    return viewport()->height() / m_lineHeight;
}

void HexViewWidget::updateScrollBar()
{
    int totalLines = lineCount();
    int visible = visibleLines();
    verticalScrollBar()->setRange(0, qMax(0, totalLines - visible));
    verticalScrollBar()->setPageStep(visible);
}

int HexViewWidget::addressColumnWidth() const
{
    // "00000000: " = 10 chars
    return m_charWidth * 10;
}

int HexViewWidget::hexColumnWidth() const
{
    // 16 bytes: "XX " * 16 + extra space in middle = 16*3 + 1 = 49 chars
    return m_charWidth * 50;
}

int HexViewWidget::asciiColumnWidth() const
{
    // "|" + 16 chars + "|" = 18 chars
    return m_charWidth * 18;
}

int HexViewWidget::gapWidth() const
{
    return m_charWidth * 2;
}

void HexViewWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBar();
}

void HexViewWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(viewport());
    painter.setFont(m_font);

    // 背景
    painter.fillRect(viewport()->rect(), QColor(30, 30, 30));

    if (m_data.isEmpty()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(viewport()->rect(), Qt::AlignCenter, QStringLiteral("No data"));
        return;
    }

    int firstLine = verticalScrollBar()->value();
    int lastLine = qMin(firstLine + visibleLines() + 1, lineCount());

    int x0 = 4; // left margin
    int addrW = addressColumnWidth();
    int hexStart = x0 + addrW;
    int asciiStart = hexStart + hexColumnWidth() + gapWidth();

    for (int line = firstLine; line < lastLine; ++line) {
        int y = (line - firstLine) * m_lineHeight + m_lineHeight - 3;
        int offset = line * m_bytesPerLine;
        int bytesInLine = qMin(m_bytesPerLine, m_data.size() - offset);

        // 地址列
        painter.setPen(QColor(100, 100, 200));
        int64_t addr = m_baseOffset + offset;
        QString addrStr = QString::asprintf("%08llX: ", static_cast<unsigned long long>(addr));
        painter.drawText(x0, y, addrStr);

        // Hex 列
        QString hexStr;
        hexStr.reserve(m_bytesPerLine * 3 + 2);
        for (int i = 0; i < bytesInLine; ++i) {
            unsigned char byte = static_cast<unsigned char>(m_data.at(offset + i));
            hexStr += QString::asprintf("%02X ", byte);
            if (i == 7) hexStr += QChar(' '); // 中间额外空格
        }
        // 补齐空格（最后一行可能不满 16 字节）
        for (int i = bytesInLine; i < m_bytesPerLine; ++i) {
            hexStr += QStringLiteral("   ");
            if (i == 7) hexStr += QChar(' ');
        }

        painter.setPen(QColor(220, 220, 220));
        painter.drawText(hexStart, y, hexStr);

        // ASCII 列
        QString asciiStr = QStringLiteral("|");
        for (int i = 0; i < bytesInLine; ++i) {
            QChar ch(static_cast<unsigned char>(m_data.at(offset + i)));
            asciiStr += ch.isPrint() ? ch : QChar('.');
        }
        for (int i = bytesInLine; i < m_bytesPerLine; ++i) {
            asciiStr += QChar(' ');
        }
        asciiStr += QChar('|');

        painter.setPen(QColor(180, 200, 130));
        painter.drawText(asciiStart, y, asciiStr);
    }
}
