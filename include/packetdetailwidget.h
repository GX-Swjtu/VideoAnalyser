#ifndef PACKETDETAILWIDGET_H
#define PACKETDETAILWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QImage>

class PacketReader;
class QTreeWidget;
class QTabWidget;
class QSplitter;

// 自适应大小的图片标签
class ScalableImageLabel : public QLabel {
    Q_OBJECT
public:
    explicit ScalableImageLabel(QWidget *parent = nullptr);
    void setOriginalImage(const QImage &image);
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    void updateScaledPixmap();
    QImage m_originalImage;
};

class PacketDetailWidget : public QWidget {
    Q_OBJECT
public:
    explicit PacketDetailWidget(PacketReader *reader, int packetIndex, QWidget *parent = nullptr);
    ~PacketDetailWidget() = default;

    int packetIndex() const { return m_packetIndex; }

private:
    void buildMetadataTree(PacketReader *reader, int packetIndex);
    void buildContentTabs(PacketReader *reader, int packetIndex);

    int m_packetIndex;
    QTreeWidget *m_metadataTree = nullptr;
    QTabWidget *m_contentTabs = nullptr;
    QSplitter *m_splitter = nullptr;
};

#endif // PACKETDETAILWIDGET_H
