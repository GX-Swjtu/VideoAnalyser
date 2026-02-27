#ifndef HEXVIEWWIDGET_H
#define HEXVIEWWIDGET_H

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QFont>

class HexViewWidget : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit HexViewWidget(QWidget *parent = nullptr);

    void setData(const QByteArray &data);
    void setBaseOffset(int64_t offset);

    const QByteArray &data() const { return m_data; }
    int64_t baseOffset() const { return m_baseOffset; }
    int bytesPerLine() const { return m_bytesPerLine; }
    int lineCount() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateScrollBar();
    int visibleLines() const;

    // 布局计算
    int addressColumnWidth() const;
    int hexColumnWidth() const;
    int asciiColumnWidth() const;
    int gapWidth() const;

    QByteArray m_data;
    int64_t m_baseOffset = 0;
    QFont m_font;
    int m_charWidth = 0;
    int m_lineHeight = 0;
    int m_bytesPerLine = 16;
};

#endif // HEXVIEWWIDGET_H
