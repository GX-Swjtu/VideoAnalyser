#ifndef MEDIAINFOWIDGET_H
#define MEDIAINFOWIDGET_H

#include <QWidget>
#include <QVector>
#include <QString>

struct PacketInfo;
struct StreamInfo;
struct AVFormatContext;

class QTreeWidget;
class QTreeWidgetItem;

class MediaInfoWidget : public QWidget {
    Q_OBJECT
public:
    explicit MediaInfoWidget(QWidget *parent = nullptr);

    /// 设置媒体信息（打开文件后调用）
    void setMediaInfo(const QString &filePath,
                      const QVector<StreamInfo> &streams,
                      AVFormatContext *fmtCtx,
                      const QVector<PacketInfo> &packets);

    /// 清空内容
    void clear();

    // ===== 静态工具方法（公开供测试） =====

    /// 格式化文件大小（字节 → "xxx.xxx MB"）
    static QString formatFileSize(int64_t bytes);

    /// 格式化时长（秒 → "HH:MM:SS (total N s)"）
    static QString formatDuration(double seconds);

    /// 格式化码率（bps → "x.xxx Mbps" 或 "x Kbps"）
    static QString formatBitrate(int64_t bps);

    /// 计算指定视频流的平均 GOP 大小
    static double computeAverageGopSize(const QVector<PacketInfo> &packets, int streamIndex);

    /// 化简宽高比 "[num:den]"
    static QString formatAspectRatio(int num, int den);

private:
    /// 添加分区标题（带背景色）
    QTreeWidgetItem *addSection(const QString &title, const QColor &bgColor);

    /// 在指定分区下添加键值对
    void addField(QTreeWidgetItem *section, const QString &key, const QString &value);

    QTreeWidget *m_tree = nullptr;
};

#endif // MEDIAINFOWIDGET_H
