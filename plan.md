# VideoAnalyser — Qt+FFmpeg 通用视频 Packet 分析器

## 项目目标

基于 Qt 6 + FFmpeg 构建一个通用视频数据分析工具。核心功能：打开任意格式视频文件 → 使用 FFmpeg 遍历所有 AVPacket → 在表格中展示 Packet 列表 → 双击某个 Packet 在新标签页中查看详情（元数据、Hex 原始数据、解码内容）。

### 核心特性

- **通用格式支持**：mp4, mkv, flv, avi, ts, mov, wmv, webm 等所有 FFmpeg 支持的封装格式
- **Packet 列表**：类似截图中 FLV 分析器的表格视图，展示每个 Packet 的类型、序号、偏移、大小、编解码器、时间戳、标志等
- **标签页模式**：主标签为 Packet 列表，双击打开新的可关闭详情标签页
- **Packet 详情**：元数据树形展示 + Hex 十六进制视图 + 解码内容
- **视频帧解码显示**：解码后以静态图片（QLabel + QImage）展示，P/B 帧从关键帧追帧解码
- **音频波形显示**：解码后以自定义波形控件可视化展示
- **字幕文本显示**：解码后以文本形式展示
- **内存友好**：只存元数据，原始数据和解码均按需从文件读取

---

## 技术决策

| 决策项 | 选择 | 理由 |
|---|---|---|
| UI 布局 | QTabWidget 标签页模式 | 用户选择 |
| 视频帧显示 | QLabel + QImage 静态帧 | 用户选择，实现简单 |
| 音频展示 | 自定义波形绘制 QWidget | 用户选择 |
| Hex 视图 | 自定义 QAbstractScrollArea | 高性能，支持大数据 |
| 数据存储策略 | 内存只存元数据，按需从文件读取原始数据 | 用户选择，大文件友好 |
| P/B 帧解码 | av_seek_frame 到最近关键帧 → 连续送帧追帧解码 → PTS 匹配目标帧 | 正确解码非关键帧的唯一方式 |
| 额外 Qt 模块 | Widgets + Concurrent + Charts | Concurrent 用于异步解码，Charts 用于时间戳/码率/AVSync 图表 |
| C++ 标准 | C++17 | 已配置 |
| 单元测试框架 | Google Test (gtest) | vcpkg 已安装，CMake 原生支持 |
| 依赖管理 | vcpkg | 需要新库（如 OpenCV）时通过 vcpkg.json 添加 |
| 视频/音频解码 | QtConcurrent::run + QFutureWatcher 异步解码 | 详情窗口立即弹出，解码在后台线程执行，不阻塞 UI |
| PacketDecoder 架构 | 可实例化类，持有独立 AVFormatContext | 避免每次解码重新 open + find_stream_info 的数百毫秒开销 |
| SwsContext 复用 | PacketDecoder 实例内缓存 SwsContext | 同分辨率/像素格式帧转换时复用，减少重复分配 |
| Hex 数据加载 | 懒加载，切到 Hex tab 时才读取 | 减少详情窗口构造时的 I/O 开销 |

---

## 设计原则

### 原则一：优先使用成熟库函数，避免自造轮子

**核心思想**：尽量调用 FFmpeg、Qt 等第三方库的成熟 API，不自己实现复杂算法。

| 功能 | 做法 | 避免 |
|---|---|---|
| 像素格式转换 | `sws_scale` / `sws_scale_frame`（FFmpeg） | 自己写 YUV→RGB 转换 |
| 音频重采样/格式转换 | `swr_convert`（FFmpeg libswresample） | 手动处理 planar/interleaved/int16/float 转换 |
| 时间戳转换 | `av_q2d()`、`av_rescale_q()`（FFmpeg） | 手动计算 time_base 换算 |
| 二分查找关键帧 | `std::lower_bound` / `std::upper_bound`（STL） | 手写二分查找 |
| 字符串格式化 | `QString::asprintf()`、`QString::number()` | 手动拼接十六进制字符串 |
| 时间格式化 | `QTime::fromMSecsSinceStartOfDay().toString("HH:mm:ss.zzz")` | 手动拼接时分秒 |
| Hex 数据格式化 | `QByteArray::toHex()` + 简单分割 | 逐字节手动转换 |
| 表格排序/筛选 | `QSortFilterProxyModel`（Qt） | 手动排序 QVector |
| 图像缩放显示 | `QPixmap::scaled(Qt::KeepAspectRatio, Qt::SmoothTransformation)` | 手动缩放算法 |
| 波形绘制 | `QPainterPath` + `QPainter::drawPath()`（Qt） | 逐像素计算绘制 |
| 进度对话框 | `QProgressDialog`（Qt） | 自建进度条 |
| 错误码转文字 | `av_strerror()`（FFmpeg） | 手动维护错误码映射表 |
| 编解码器名称 | `avcodec_get_name()`（FFmpeg） | 手动维护 codec_id→名称映射 |
| 媒体类型名称 | `av_get_media_type_string()`（FFmpeg） | 手动 switch-case |

### 原则二：所有自写函数必须有单元测试

**规则**：
- 每个 `.cpp` 源文件对应一个 `tests/test_<模块名>.cpp` 测试文件
- 使用 Google Test (gtest) 框架，项目中已通过 vcpkg 安装
- 测试覆盖：正常路径 + 边界条件 + 错误处理
- 涉及 FFmpeg 文件操作的测试使用小型测试视频文件（放在 `tests/testdata/` 目录）
- 纯数据处理/格式化函数可直接单元测试，UI 控件测试侧重数据逻辑而非渲染

### 原则三：可通过 vcpkg 按需引入新库

如果后续发现某些功能用现有库实现困难（例如更复杂的图像处理），可以在 `vcpkg.json` 中添加新依赖（如 OpenCV），通过 vcpkg 自动安装和链接。当前已安装的 vcpkg 依赖：
- `ffmpeg`（含 avcodec, avdevice, avfilter, avformat, swresample, swscale）
- `gtest`（Google Test 单元测试框架）

---

## 当前项目状态

- **构建基础设施完备**：CMake + vcpkg + Qt 6.8.3 + FFmpeg 8.0.1 + MinGW 13.x + Ninja
- **核心功能完成**：PacketReader、PacketListModel、PacketDecoder、PacketDetailWidget、HexViewWidget、AudioWaveformWidget、AudioSpectrogramWidget 均已实现并通过测试
- **分析页面完成**：MediaInfoWidget、TimestampChartWidget、BitrateChartWidget、AVSyncChartWidget、LogAnalysisWidget 五大分析面板已实现并通过测试
- **主窗口六标签页**：Data Table、Media Info、Timestamp、Bitrate、AVSync、Log 全部集成
- **CMake GLOB_RECURSE**：自动收集 `src/*.cpp` 和 `include/*.h`，新增文件无需修改 CMakeLists.txt
- **单元测试**：142 个测试全部通过

---

## 文件结构规划

```
include/
    mainwindow.h              # [已实现] 主窗口，管理六大分析标签页
    packetreader.h             # [已实现] FFmpeg 解封装，遍历 Packet，维护关键帧索引
    packetlistmodel.h          # [已实现] QAbstractTableModel，驱动 Packet 列表表格
    packetdetailwidget.h       # [已实现] 单个 Packet 的详情标签页
    packetdecoder.h            # [已实现] 封装解码逻辑（视频追帧、音频、字幕）
    hexviewwidget.h            # [已实现] 十六进制数据查看控件
    audiowaveformwidget.h      # [已实现] 音频波形绘制控件
    audiospectrogramwidget.h   # [已实现] 音频频谱图控件
    mediainfowidget.h          # [已实现] 媒体信息分析面板
    timestampchartwidget.h     # [已实现] 时间戳分析 QtCharts 图表
    bitratechartwidget.h       # [已实现] 码率分析 QtCharts 图表
    avsyncchartwidget.h        # [已实现] 音视频同步分析 QtCharts 图表
    loganalysiswidget.h        # [已实现] FFmpeg 日志分析面板
src/
    main.cpp                   # [不变] 应用入口
    mainwindow.cpp             # [已实现] 主窗口逻辑实现
    packetreader.cpp           # [已实现]
    packetlistmodel.cpp        # [已实现]
    packetdetailwidget.cpp     # [已实现]
    packetdecoder.cpp          # [已实现]
    hexviewwidget.cpp          # [已实现]
    audiowaveformwidget.cpp    # [已实现]
    audiospectrogramwidget.cpp # [已实现]
    mediainfowidget.cpp        # [已实现]
    timestampchartwidget.cpp   # [已实现]
    bitratechartwidget.cpp     # [已实现]
    avsyncchartwidget.cpp      # [已实现]
    loganalysiswidget.cpp      # [已实现]
ui/
    mainwindow.ui              # [修改] 或完全用代码构建 UI
tests/
    CMakeLists.txt             # [已实现] 测试构建配置（find gtest, add test targets）
    test_packetreader.cpp      # [已实现] PacketReader 单元测试
    test_packetlistmodel.cpp   # [已实现] PacketListModel 单元测试
    test_packetdecoder.cpp     # [已实现] PacketDecoder 单元测试
    test_hexviewwidget.cpp     # [已实现] HexViewWidget 数据逻辑测试
    test_audiowaveformwidget.cpp # [已实现] AudioWaveformWidget 数据逻辑测试
    test_audiospectrogramwidget.cpp # [已实现] AudioSpectrogramWidget 测试
    test_mediainfowidget.cpp   # [已实现] MediaInfoWidget 测试
    test_timestampchartwidget.cpp # [已实现] TimestampChartWidget 测试
    test_bitratechartwidget.cpp  # [已实现] BitrateChartWidget 测试
    test_avsyncchartwidget.cpp # [已实现] AVSyncChartWidget 测试
    test_loganalysiswidget.cpp # [已实现] LogAnalysisWidget 测试
    test_utils.cpp             # [已实现] 工具函数单元测试
    testdata/                  # [已实现] 测试用小视频文件目录
        test_h264_aac.mp4      # 含 H264 视频 + AAC 音频的短测试文件（<1MB）
```

---

## 实施步骤

### Phase 1：数据层 — PacketReader

**目标**：封装 FFmpeg 解封装逻辑，能打开视频文件、遍历所有 Packet 存储元数据、维护关键帧索引、支持按需读取原始数据。

**文件**：`include/packetreader.h` + `src/packetreader.cpp`

**数据结构**：

```cpp
// 单个 Packet 的元数据（不含原始数据）
struct PacketInfo {
    int index;                // 全局序号（从 0 开始）
    int streamIndex;          // 所属流索引
    AVMediaType mediaType;    // AVMEDIA_TYPE_VIDEO / AUDIO / SUBTITLE / DATA
    QString codecName;        // 编解码器名称，如 "h264", "aac"
    int64_t pts;              // 显示时间戳（原始值，time_base 单位）
    int64_t dts;              // 解码时间戳（原始值）
    double ptsTime;           // PTS 转换为秒
    double dtsTime;           // DTS 转换为秒
    int size;                 // 数据大小（字节）
    int64_t pos;              // 文件字节偏移（-1 为未知）
    int flags;                // 标志位：AV_PKT_FLAG_KEY, AV_PKT_FLAG_CORRUPT 等
    int64_t duration;         // 持续时间（time_base 单位）
    double durationTime;      // 持续时间（秒）
    int gopKeyFrameIndex;     // 所属 GOP 的关键帧在全局列表中的序号（视频流用）
};

// 流信息
struct StreamInfo {
    int index;                // 流索引
    AVMediaType mediaType;    // 媒体类型
    QString codecName;        // 编解码器名称
    AVCodecParameters *codecpar; // 编解码参数（深拷贝，需手动释放）
    AVRational timeBase;      // 时间基
    AVRational avgFrameRate;  // 平均帧率（视频流）
    int64_t duration;         // 流时长（time_base 单位）
    int64_t nbFrames;         // 帧数
    int sampleRate;           // 采样率（音频流）
    int channels;             // 声道数（音频流）
    QString title;            // 流标题（来自 metadata）
    QString language;         // 语言（来自 metadata）
};
```

**PacketReader 类接口**：

```cpp
class PacketReader : public QObject {
    Q_OBJECT
public:
    bool open(const QString &filePath);
    bool readAllPackets();   // 遍历所有 Packet，填充 m_packets
    void close();

    // 访问器
    int packetCount() const;
    const PacketInfo &packetAt(int index) const;
    const QVector<PacketInfo> &packets() const;
    const QVector<StreamInfo> &streams() const;
    QString formatName() const;      // 封装格式名称
    double durationSeconds() const;  // 总时长（秒）
    int64_t bitRate() const;         // 总比特率

    // 按需读取原始数据（用于 Hex 视图）
    QByteArray readPacketData(int packetIndex);

    // 获取 AVFormatContext（解码器需要用）
    AVFormatContext *formatContext() const;

    // 关键帧索引查询
    int findGopKeyFrame(int packetIndex) const;

signals:
    void progressChanged(int current, int total);  // 读取进度

private:
    AVFormatContext *m_formatCtx = nullptr;
    QVector<PacketInfo> m_packets;
    QVector<StreamInfo> m_streams;
    QMap<int, QVector<int>> m_keyFrameIndices;  // streamIndex → 关键帧全局序号列表
    QString m_filePath;
};
```

**关键实现细节**：

1. `readAllPackets()` 中，每读到一个 Packet：
   - 提取元数据填入 `PacketInfo`
   - 如果是关键帧（`flags & AV_PKT_FLAG_KEY`），记录到 `m_keyFrameIndices[streamIndex]`
   - 为视频流 Packet 计算 `gopKeyFrameIndex`（在 `m_keyFrameIndices[streamIndex]` 中二分查找 ≤ 当前 index 的最大值）
   - `av_packet_unref` 释放原始数据
   - 发射 `progressChanged` 信号（每 1000 个 Packet 或按百分比）

2. `readPacketData()` 实现：
   - `av_seek_frame(m_formatCtx, packetInfo.streamIndex, packetInfo.dts, AVSEEK_FLAG_BACKWARD)`
   - 循环 `av_read_frame` 直到找到 `streamIndex` 和 `pos` 都匹配的 Packet
   - 返回 `QByteArray(pkt->data, pkt->size)`
   - `av_packet_unref` 释放

3. `StreamInfo.codecpar` 使用 `avcodec_parameters_alloc()` + `avcodec_parameters_copy()` 深拷贝，在 `close()` 时 `avcodec_parameters_free()` 释放

4. 时间转换：`ptsTime = pts * av_q2d(stream->time_base)`（使用 FFmpeg 的 `av_q2d()` 而非手动计算）

5. 编解码器名称：使用 `avcodec_get_name(codecpar->codec_id)` 获取，而非手动映射

6. 错误处理：所有 FFmpeg 调用失败时使用 `av_strerror(ret, buf, sizeof(buf))` 获取错误描述

**单元测试（`tests/test_packetreader.cpp`）**：

| 测试用例 | 说明 |
|---|---|
| `OpenValidFile` | 打开有效 mp4 文件，验证 `open()` 返回 true |
| `OpenInvalidFile` | 打开不存在/损坏文件，验证 `open()` 返回 false 且不崩溃 |
| `ReadAllPackets` | 读取测试文件所有 Packet，验证 `packetCount() > 0` |
| `PacketMetadataFields` | 验证 PacketInfo 各字段合理：`size > 0`、`streamIndex` 在有效范围、`mediaType` 正确 |
| `StreamInfoCorrect` | 验证 StreamInfo：视频流有 width/height > 0，音频流有 sampleRate > 0 |
| `KeyFrameIndexTable` | 验证关键帧索引表非空；第一个视频 Packet 的 `gopKeyFrameIndex` 应指向自身或前序关键帧 |
| `FindGopKeyFrame` | 验证 `findGopKeyFrame()` 返回值 ≤ 目标 index，且返回的 Packet 为关键帧 |
| `ReadPacketData` | 验证 `readPacketData()` 返回的 `QByteArray::size()` 等于 `PacketInfo::size` |
| `TimeConversion` | 验证 `ptsTime`/`dtsTime` 为非负数，且在文件时长范围内 |
| `CloseAndReopen` | 关闭后重新打开，验证不泄漏资源 |

---

### Phase 2：表格模型 — PacketListModel

**目标**：将 PacketReader 的数据适配为 Qt 表格模型，驱动 QTableView 显示。

**文件**：`include/packetlistmodel.h` + `src/packetlistmodel.cpp`

**列定义**（10 列）**：

| 列索引 | 列名 | 内容示例 | 说明 |
|---|---|---|---|
| 0 | Type | 🎬 / 🔊 / 📝 / 📦 | 媒体类型，用背景色或图标区分 |
| 1 | Index | 0, 1, 2, ... | 全局 Packet 序号 |
| 2 | Stream | 0, 1 | 流索引 |
| 3 | Offset | 0x0002B3D2 | 文件偏移，十六进制 |
| 4 | Size | 53171 | 数据大小（字节） |
| 5 | Flags | KEY | KEY/CORRUPT/DISCARD 标记 |
| 6 | Codec | h264, aac | 编解码器名称 |
| 7 | PTS | 00:00:00.040 (40) | HH:MM:SS.mmm 格式 + 括号中原始值 |
| 8 | DTS | 00:00:00.040 (40) | 同上 |
| 9 | Duration | 0.040 | 持续时间（秒），保留 3 位小数 |

**实现要点**：

```cpp
class PacketListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColType = 0, ColIndex, ColStream, ColOffset, ColSize,
        ColFlags, ColCodec, ColPTS, ColDTS, ColDuration,
        ColCount
    };

    void setPackets(const QVector<PacketInfo> &packets);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation, int role) const override;

private:
    QVector<PacketInfo> m_packets;  // 引用或拷贝 PacketReader 的数据
};
```

- `Qt::DisplayRole`：格式化文本
  - 时间格式化使用 `QTime::fromMSecsSinceStartOfDay(ms).toString("HH:mm:ss.zzz")`，不手动拼接
  - 偏移地址使用 `QString::asprintf("0x%08llX", pos)`，不手动转换
  - 媒体类型名称使用 `av_get_media_type_string()`
- `Qt::BackgroundRole`：Type 列按媒体类型着色（视频=浅蓝、音频=浅绿、字幕=浅黄、数据=浅灰）
- `Qt::TextAlignmentRole`：数字列右对齐
- `Qt::DecorationRole`：Type 列可选图标

**单元测试（`tests/test_packetlistmodel.cpp`）**：

| 测试用例 | 说明 |
|---|---|
| `EmptyModel` | 空模型 `rowCount() == 0`、`columnCount() == ColCount` |
| `SetPackets` | 设置数据后 `rowCount()` 正确 |
| `DisplayRoleData` | 验证各列的 `DisplayRole` 返回正确格式化文本 |
| `BackgroundRoleColor` | 视频行返回浅蓝色、音频行返回浅绿色 |
| `HeaderData` | 验证列标题文本正确 |
| `TimeFormatting` | PTS = 40040ms → "00:00:40.040 (40040)" |
| `OffsetFormatting` | pos = 0x2B3D2 → "0x0002B3D2" |
| `FlagsFormatting` | KEY 标志正确显示 |
| `FilterByMediaType` | ProxyModel 过滤后只显示指定类型的行 |
| `FilterByStream` | ProxyModel 按流索引过滤正确 |

**配合 QSortFilterProxyModel**：

```cpp
class PacketFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    void setMediaTypeFilter(AVMediaType type);  // -1 表示全部
    void setStreamIndexFilter(int streamIndex);  // -1 表示全部
protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &parent) const override;
private:
    AVMediaType m_mediaTypeFilter = AVMEDIA_TYPE_UNKNOWN;  // 默认不过滤
    int m_streamIndexFilter = -1;
};
```

---

### Phase 3：Hex 视图 — HexViewWidget

**目标**：高性能十六进制数据查看器，类似截图中左侧的 Hex 面板。

**文件**：`include/hexviewwidget.h` + `src/hexviewwidget.cpp`

**布局**（每行）：

```
00000000: 09 00 DD 50 00 00 C8 00  00 00 00 00 02 09 30 00  |...P..........0.|
```

- 左列：8 位十六进制地址偏移
- 中列：16 字节 Hex 数据，每 8 字节一组，中间额外空格
- 右列：ASCII 字符（不可见字符用 `.` 替代），用 `|` 包围

**实现要点**：

```cpp
class HexViewWidget : public QAbstractScrollArea {
    Q_OBJECT
public:
    void setData(const QByteArray &data);
    void setBaseOffset(int64_t offset);  // 设置起始地址偏移（用于显示文件中的实际地址）

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QByteArray m_data;
    int64_t m_baseOffset = 0;
    QFont m_font;           // 等宽字体 Consolas / Courier New
    int m_charWidth;        // 单个字符宽度
    int m_lineHeight;       // 行高
    int m_bytesPerLine = 16;

    // 计算布局参数
    int addressColumnWidth() const;
    int hexColumnWidth() const;
    int asciiColumnWidth() const;
};
```

- 字体：`QFontDatabase::systemFont(QFontDatabase::FixedFont)`（使用 Qt API 获取系统等宽字体）
- 使用 `QAbstractScrollArea::verticalScrollBar()` 管理滚动
- `paintEvent` 只绘制可见区域的行（根据 scrollbar 位置计算起始行/结束行）
- 选中高亮：鼠标点击/拖拽选中字节范围，绘制时对选中区域用不同背景色
- **Hex 格式化**：使用 `QByteArray::toHex(' ')` 生成十六进制文本（Qt 内置），避免逐字节手动转换
- **ASCII 转换**：使用 `QChar::isPrint()` 判断可见字符，不手写 ASCII 范围判断

**单元测试（`tests/test_hexviewwidget.cpp`）**：

| 测试用例 | 说明 |
|---|---|
| `SetDataAndSize` | 设置数据后内部状态正确 |
| `BaseOffset` | 设置 baseOffset 后地址显示正确 |
| `EmptyData` | 空数据不崩溃 |
| `LargeData` | 1MB 数据可正常设置，不 OOM |
| `LineCount` | 数据长度 / 16 向上取整 = 总行数 |

---

### Phase 4：音频波形 — AudioWaveformWidget

**目标**：将解码后的音频 PCM 采样数据以波形图可视化。

**文件**：`include/audiowaveformwidget.h` + `src/audiowaveformwidget.cpp`

**实现要点**：

```cpp
struct AudioData {
    QVector<float> samples;  // 交错排列的浮点采样（-1.0 ~ 1.0）
    int sampleRate;
    int channels;
};

class AudioWaveformWidget : public QWidget {
    Q_OBJECT
public:
    void setAudioData(const AudioData &data);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    AudioData m_data;
};
```

- **多声道分行显示**：每个声道占据控件高度的等分区域
- **绘制**：对每个声道，X 轴 = 采样点索引映射到控件宽度，Y 轴 = 振幅映射到区域高度中心线上下
- **颜色**：暗灰背景 (`#1E1E1E`)，波形用亮绿色 (`#00FF00`) 或不同声道用不同颜色
- **中心线**：每个声道绘制一条灰色水平中心线
- **信息标注**：左上角显示 "采样率: 44100Hz | 声道: 2 | 采样数: 1024 | 时长: 0.023s"
- **采样点过多时降采样**：如果采样点数 > 控件宽度像素数 × 2，对每段取 min/max 绘制（使用 `std::minmax_element` 而非手写循环）
- **绘制方式**：使用 `QPainterPath::lineTo()` 构建路径 + `QPainter::drawPath()` 一次性绘制（比逐段 drawLine 高效，Qt 内部优化路径渲染）
- **坐标映射**：使用 `QTransform` 或简单线性映射（采样索引→X，振幅→Y），不写复杂的坐标算法

**单元测试（`tests/test_audiowaveformwidget.cpp`）**：

| 测试用例 | 说明 |
|---|---|
| `SetAudioData` | 设置数据后状态正确 |
| `EmptyData` | 空数据不崩溃 |
| `DownsampleLogic` | 验证降采样逻辑：N 个采样点在 W 像素宽度下正确分段 |
| `MultiChannel` | 多声道数据正确分离 |

---

### Phase 5：Packet 解码器 — PacketDecoder

**目标**：封装单个 Packet 的解码逻辑，支持视频追帧解码、音频解码、字幕解码。

**文件**：`include/packetdecoder.h` + `src/packetdecoder.cpp`

**接口**（实例类，持有独立 AVFormatContext 以复用）：

```cpp
class PacketDecoder {
public:
    explicit PacketDecoder(PacketReader *reader);
    ~PacketDecoder();

    // 禁止拷贝
    PacketDecoder(const PacketDecoder &) = delete;
    PacketDecoder &operator=(const PacketDecoder &) = delete;

    // 打开独立的 AVFormatContext（仅需调用一次）
    bool open(QString *errorMsg = nullptr);
    void close();
    bool isOpen() const;

    // 视频解码（追帧）- 返回解码后的 QImage，失败返回空 QImage
    QImage decodeVideoPacket(int packetIndex, QString *errorMsg = nullptr);

    // 音频解码 - 返回 PCM 浮点数据
    AudioData decodeAudioPacket(int packetIndex, QString *errorMsg = nullptr);

    // 字幕解码 - 返回文本（不使用 m_fmtCtx，通过 readPacketData 读原始数据）
    QString decodeSubtitlePacket(int packetIndex, QString *errorMsg = nullptr);

    static QString ffmpegError(int errnum);

private:
    PacketReader *m_reader = nullptr;
    AVFormatContext *m_fmtCtx = nullptr;     // 独立的格式上下文，open() 时创建，close() 时销毁
    SwsContext *m_swsCtx = nullptr;          // 缓存的色彩转换上下文
    int m_swsWidth = 0, m_swsHeight = 0;
    int m_swsSrcFmt = -1;
};
```

**性能优化设计**：

| 优化项 | 旧实现 | 新实现 | 效果 |
|---|---|---|---|
| AVFormatContext | 每次解码 open + find_stream_info + close | 实例持有，open() 一次复用 | 省去数百毫秒/次 |
| SwsContext | 每次解码 sws_getContext + sws_freeContext | 实例内缓存，参数不变时复用 | 减少重复分配 |
| 线程模型 | 同步阻塞 UI 主线程 | QtConcurrent::run 后台解码 | UI 不冻结 |
| Hex 数据 | 构造时立即 readPacketData | 切到 Hex tab 时才读取 | 减少构造时 I/O |

**生命周期管理**：在异步解码场景中，`PacketDecoder` 通过 `std::shared_ptr` 传入 `QtConcurrent::run` 的 lambda，lambda 执行完毕后自动析构释放资源。如果用户关闭窗口，`QFutureWatcher` 析构时会 `waitForFinished()`，保证安全。

#### 5a. 视频解码（关键帧追帧）

**完整流程**：

```
decodeVideoPacket(reader, targetIndex):
    1. 获取 targetPkt = reader->packetAt(targetIndex)
    2. 获取 streamInfo = reader->streams()[targetPkt.streamIndex]
    3. gopStart = reader->findGopKeyFrame(targetIndex)
       // 使用 std::upper_bound 在 m_keyFrameIndices[streamIndex] 中查找

    4. 找到解码器（全部使用 FFmpeg API，无自定义逻辑）：
       codec = avcodec_find_decoder(streamInfo.codecpar->codec_id)
       codecCtx = avcodec_alloc_context3(codec)
       avcodec_parameters_to_context(codecCtx, streamInfo.codecpar)
       avcodec_open2(codecCtx, codec, NULL)

    5. Seek 到关键帧位置（FFmpeg 内部处理 seek 精度）：
       gopPkt = reader->packetAt(gopStart)
       av_seek_frame(reader->formatContext(), targetPkt.streamIndex, gopPkt.dts, AVSEEK_FLAG_BACKWARD)

    6. 连续读取并送入解码器：
       while (av_read_frame(formatCtx, pkt) >= 0):
           if pkt->stream_index != targetPkt.streamIndex:
               av_packet_unref(pkt)
               continue

           avcodec_send_packet(codecCtx, pkt)

           while (avcodec_receive_frame(codecCtx, frame) >= 0):
               if frame->pts == targetPkt.pts:
                   // 找到目标帧！
                   → sws_scale 转 RGB24 → QImage
                   → 返回
               av_frame_unref(frame)

           // 如果已经超过目标 Packet 的 DTS，可以停止
           if pkt->dts > targetPkt.dts + 一定容差:
               break
           av_packet_unref(pkt)

    7. 如果遍历完仍未找到，返回空 QImage + 错误信息
       使用 av_strerror() 获取错误描述

    8. 清理：avcodec_free_context, sws_freeContext 等
```

**sws_scale 转 RGB 的步骤**（全部使用 FFmpeg API）：

```cpp
// 方案 A：使用 FFmpeg 8.0 新 API（推荐，更简洁）
AVFrame *rgbFrame = av_frame_alloc();
rgbFrame->format = AV_PIX_FMT_RGB24;
rgbFrame->width = frame->width;
rgbFrame->height = frame->height;
av_frame_get_buffer(rgbFrame, 0);  // FFmpeg 自动分配内存

SwsContext *swsCtx = sws_getContext(
    frame->width, frame->height, (AVPixelFormat)frame->format,
    frame->width, frame->height, AV_PIX_FMT_RGB24,
    SWS_BILINEAR, nullptr, nullptr, nullptr);
sws_scale(swsCtx, frame->data, frame->linesize, 0, frame->height,
          rgbFrame->data, rgbFrame->linesize);

// 用 FFmpeg 分配的 buffer 构造 QImage
QImage image(rgbFrame->data[0], rgbFrame->width, rgbFrame->height,
             rgbFrame->linesize[0], QImage::Format_RGB888);
QImage result = image.copy();  // 深拷贝
av_frame_free(&rgbFrame);
sws_freeContext(swsCtx);

// 方案 B：使用 sws_scale_frame（FFmpeg 8.0，最简洁）
// sws_scale_frame(swsCtx, rgbFrame, frame);  // 一行搞定
```

#### 5b. 音频解码

```
decodeAudioPacket(reader, targetIndex):
    1. 获取目标 PacketInfo 和 StreamInfo
    2. 创建音频解码器上下文（同视频流程，全用 FFmpeg API）
    3. av_seek_frame 到目标 Packet 附近
    4. av_read_frame 定位到目标 Packet（匹配 streamIndex + pts/dts）
    5. avcodec_send_packet → avcodec_receive_frame
    6. 采样格式转换（全部交给 FFmpeg libswresample，不手动处理）：
       如果 frame->format != AV_SAMPLE_FMT_FLT:
         SwrContext *swr = nullptr;
         swr_alloc_set_opts2(&swr,
             &frame->ch_layout, AV_SAMPLE_FMT_FLT, frame->sample_rate,  // 输出
             &frame->ch_layout, frame->format, frame->sample_rate,       // 输入
             0, nullptr);
         swr_init(swr);
         swr_convert(swr, &outBuf, nb_samples, frame->extended_data, nb_samples);
         swr_free(&swr);
    7. 拷贝采样数据到 QVector<float>
    8. 返回 AudioData { samples, sampleRate, channels }
    9. 清理资源
```

#### 5c. 字幕解码

```
decodeSubtitlePacket(reader, targetIndex):
    1. 读取目标 Packet 原始数据：reader->readPacketData(targetIndex)
    2. 创建字幕解码器上下文（FFmpeg API）
    3. 构造 AVPacket，填入数据
    4. avcodec_decode_subtitle2(codecCtx, &subtitle, &got_sub, pkt)
    5. 如果 got_sub:
       遍历 subtitle.rects[i]:
         如果 type == SUBTITLE_TEXT → 提取 text
         如果 type == SUBTITLE_ASS → 提取 ass 文本
    6. avsubtitle_free(&subtitle)
    7. 返回 QString
```

**单元测试（`tests/test_packetdecoder.cpp`）**：

| 测试用例 | 说明 |
|---|---|
| `DecodeKeyFrame` | 解码关键帧（I帧），返回非空 QImage |
| `DecodeKeyFrameDimensions` | 解码后图像 width/height 与 StreamInfo 一致 |
| `DecodePBFrame` | 解码 P/B 帧（追帧），返回非空 QImage |
| `DecodeAudioPacket` | 解码音频 Packet，返回 samples 非空、sampleRate > 0、channels > 0 |
| `DecodeAudioFormat` | 验证返回的 float 采样在 -1.0 ~ 1.0 范围内（或合理范围） |
| `DecodeAudioPacketNonSilent` | 非首个音频包解码后最大振幅 > 0.001（非静音） |
| `InvalidPacketIndex` | 传入越界索引，返回空结果 + 错误信息，不崩溃 |
| `VideoStreamOnlyPacket` | 对音频 Packet 调用 decodeVideoPacket，返回空 QImage |
| `NullReader` | 传入 nullptr reader，各方法返回空结果 + 错误信息 |
| `ReuseContextMultipleDecodes` | 同一实例连续解码多个不同 packet，验证 Context 复用正确性 |
| `OpenCloseCycle` | open → 解码 → close → 重新 open → 解码，验证生命周期管理 |

---

### Phase 6：Packet 详情页 — PacketDetailWidget

**目标**：展示单个 Packet 的完整详情，包含元数据、Hex 视图、解码内容。

**文件**：`include/packetdetailwidget.h` + `src/packetdetailwidget.cpp`

**布局**：

```
┌─ PacketDetailWidget ─────────────────────────────────────────────────┐
│ QSplitter (Horizontal)                                               │
│ ┌─ 左侧：内容标签页 ──────────────────┐ ┌─ 右侧：元数据 ──────────┐ │
│ │ QTabWidget                          │ │ QTreeWidget              │ │
│ │ ┌─[Hex]──[视频帧]──[波形]──[字幕]─┐ │ │ ├─ 基本信息              │ │
│ │ │                                  │ │ │ │  ├─ Packet 序号: 18   │ │
│ │ │ (根据媒体类型动态添加标签)        │ │ │ │  ├─ 流索引: 0          │ │
│ │ │                                  │ │ │ │  ├─ 媒体类型: Video    │ │
│ │ │                                  │ │ │ │  └─ 编解码器: h264     │ │
│ │ │                                  │ │ │ ├─ 时间信息              │ │
│ │ │                                  │ │ │ │  ├─ PTS: 200          │ │
│ │ │                                  │ │ │ │  ├─ DTS: 200          │ │
│ │ │                                  │ │ │ │  └─ Duration: 40      │ │
│ │ │                                  │ │ │ ├─ 文件信息              │ │
│ │ │                                  │ │ │ ├─ 标志                  │ │
│ │ │                                  │ │ │ └─ GOP 信息（视频流）    │ │
│ │ └──────────────────────────────────┘ │ │                          │ │
│ └──────────────────────────────────────┘ └──────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────┘
```

**构造逻辑**（异步解码 + 懒加载）：

```cpp
PacketDetailWidget::PacketDetailWidget(PacketReader *reader, int packetIndex, QWidget *parent)
{
    // ... 构建 QSplitter + QTabWidget + QTreeWidget 布局 ...
    buildMetadataTree(reader, packetIndex);  // 元数据树（纯内存操作，同步）
    buildContentTabs(reader, packetIndex);   // 内容标签页（异步解码）

    // Hex 懒加载：切换 tab 时按需读取
    connect(m_contentTabs, &QTabWidget::currentChanged, this, &PacketDetailWidget::onTabChanged);
}

void PacketDetailWidget::buildContentTabs(PacketReader *reader, int packetIndex)
{
    // 视频帧 — 异步解码
    if (pkt.mediaType == AVMEDIA_TYPE_VIDEO) {
        m_imageLabel = new ScalableImageLabel();
        m_imageLabel->setText("正在解码...");
        m_contentTabs->addTab(m_imageLabel, "视频帧");

        // 后台线程创建独立 PacketDecoder 实例并执行解码
        auto decoder = std::make_shared<PacketDecoder>(reader);
        m_videoWatcher = new QFutureWatcher<QImage>(this);
        connect(m_videoWatcher, &QFutureWatcher<QImage>::finished,
                this, &PacketDetailWidget::onVideoDecoded);
        m_videoWatcher->setFuture(QtConcurrent::run([decoder, idx]() -> QImage {
            decoder->open();
            return decoder->decodeVideoPacket(idx, nullptr);
        }));
    }

    // 音频 — 异步解码（频谱 + 波形同时更新）
    if (pkt.mediaType == AVMEDIA_TYPE_AUDIO) {
        m_spectrogram = new AudioSpectrogramWidget();
        m_waveform = new AudioWaveformWidget();
        // ... 类似视频的 QtConcurrent::run + QFutureWatcher<AudioData> ...
    }

    // 字幕 — 同步解码（通常很快）
    if (pkt.mediaType == AVMEDIA_TYPE_SUBTITLE) { /* ... */ }

    // Hex — 懒加载：创建空 HexViewWidget，切到 tab 时才调用 readPacketData
    m_hexView = new HexViewWidget();
    m_hexTabIndex = m_contentTabs->addTab(m_hexView, "Hex");
}

void PacketDetailWidget::onTabChanged(int index)
{
    if (index == m_hexTabIndex && !m_hexLoaded) {
        m_hexView->setData(m_reader->readPacketData(m_packetIndex));
        m_hexLoaded = true;
    }
}
```

**异步解码关键成员**：

```cpp
class PacketDetailWidget : public QWidget {
    // ...
private:
    QFutureWatcher<QImage> *m_videoWatcher = nullptr;
    QFutureWatcher<AudioData> *m_audioWatcher = nullptr;
    ScalableImageLabel *m_imageLabel = nullptr;       // 视频异步更新
    AudioWaveformWidget *m_waveform = nullptr;         // 音频异步更新
    AudioSpectrogramWidget *m_spectrogram = nullptr;   // 音频异步更新
    HexViewWidget *m_hexView = nullptr;                // Hex 懒加载
    int m_hexTabIndex = -1;
    bool m_hexLoaded = false;
};
```

---

### Phase 7：主窗口 — MainWindow

**目标**：整合所有组件，实现完整的用户交互流程。

**文件**：`include/mainwindow.h` + `src/mainwindow.cpp` + `ui/mainwindow.ui`（或纯代码构建）

**成员变量**：

```cpp
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openFile();
    void onPacketDoubleClicked(const QModelIndex &index);
    void onTabCloseRequested(int tabIndex);
    void onFilterChanged(int comboIndex);

private:
    void setupUI();
    void setupMenuAndToolbar();
    void updateStatusBar();

    PacketReader *m_reader = nullptr;
    PacketListModel *m_model = nullptr;
    PacketFilterProxyModel *m_proxyModel = nullptr;

    QTabWidget *m_tabWidget = nullptr;
    QTableView *m_tableView = nullptr;
    QComboBox *m_filterCombo = nullptr;
};
```

**setupUI() 布局**：

```
┌─ MainWindow ──────────────────────────────────────┐
│ [菜单栏] 文件(F) → 打开(O)                         │
│ [工具栏] [📂打开] | 筛选: [全部 ▾]                  │
│ ┌─ QTabWidget ─────────────────────────────────┐  │
│ │ [Packet列表] [Packet#7(video)] [Packet#18..] │  │
│ │ ┌───────────────────────────────────────────┐ │  │
│ │ │ QTableView (PacketListModel)              │ │  │
│ │ │ Type│Index│Stream│Offset    │Size │Flags..│ │  │
│ │ │ 🔊  │ 6   │ 0    │0x2B3D2  │328  │      ..│ │  │
│ │ │ 🎬  │ 7   │ 1    │0x2B529  │53171│KEY   ..│ │  │
│ │ │ ...                                       │ │  │
│ │ └───────────────────────────────────────────┘ │  │
│ └──────────────────────────────────────────────┘  │
│ [状态栏] FLV | 时长: 00:05:23 | 3 streams | 15234 │
│          packets                                   │
└───────────────────────────────────────────────────┘
```

**openFile() 流程**：

```
1. QFileDialog::getOpenFileName(filter: "视频文件 (*.mp4 *.mkv *.flv *.avi *.ts *.mov *.wmv *.webm);;所有文件 (*)")
2. 如果已有打开的文件：
   a. 关闭所有详情标签页
   b. reader->close()
3. reader->open(filePath)
4. 显示 QProgressDialog
5. 连接 reader->progressChanged → progressDialog->setValue
6. reader->readAllPackets()
7. model->setPackets(reader->packets())
8. 更新状态栏
9. 设置窗口标题为文件名
```

**onPacketDoubleClicked() 流程**：

```
1. 从 proxyModel 映射回 sourceModel 获取真实行号
2. 创建 PacketDetailWidget(m_reader, packetIndex)
3. 生成标签标题: "Packet #N (video)" / "Packet #N (audio)" / ...
4. m_tabWidget->addTab(detailWidget, title)
5. m_tabWidget->setCurrentWidget(detailWidget)
```

**onTabCloseRequested() 流程**：

```
1. 如果 tabIndex == 0（Packet 列表页），忽略（不可关闭）
2. 否则 delete m_tabWidget->widget(tabIndex)
```

**筛选 ComboBox 选项**：

```
全部 / 仅视频 / 仅音频 / 仅字幕 / 仅数据
→ 设置 proxyModel 的 mediaTypeFilter
```

---

### Phase 8：单元测试基础设施

**目标**：配置 gtest 构建、编写并运行所有单元测试。

**CMake 配置**：在主 `CMakeLists.txt` 末尾添加（或在 `tests/CMakeLists.txt` 中独立配置）：

```cmake
# tests/CMakeLists.txt
enable_testing()
find_package(GTest REQUIRED)

# 将业务逻辑编译为静态库供测试链接（排除 main.cpp）
file(GLOB_RECURSE LIB_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER LIB_SOURCES EXCLUDE REGEX "main\\.cpp$")
file(GLOB_RECURSE LIB_HEADERS ${CMAKE_SOURCE_DIR}/include/*.h)

add_library(VideoAnalyserLib STATIC ${LIB_SOURCES} ${LIB_HEADERS})
target_include_directories(VideoAnalyserLib PUBLIC ${CMAKE_SOURCE_DIR}/include ${FFMPEG_INCLUDE_DIRS})
target_link_directories(VideoAnalyserLib PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(VideoAnalyserLib PUBLIC Qt6::Widgets ${FFMPEG_LIBRARIES})
set_target_properties(VideoAnalyserLib PROPERTIES AUTOMOC ON AUTOUIC ON)

# 测试可执行文件
file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
add_executable(VideoAnalyserTests ${TEST_SOURCES})
target_link_libraries(VideoAnalyserTests PRIVATE VideoAnalyserLib GTest::gtest GTest::gtest_main)

# 测试数据路径
target_compile_definitions(VideoAnalyserTests PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")

add_test(NAME VideoAnalyserTests COMMAND VideoAnalyserTests)
```

**测试文件清单**：

| 测试文件 | 覆盖模块 | 测试重点 |
|---|---|---|
| `test_packetreader.cpp` | PacketReader | 打开/关闭文件、遍历 Packet、关键帧索引、按需读取 |
| `test_packetlistmodel.cpp` | PacketListModel + FilterProxy | 数据格式化、角色返回值、筛选逻辑 |
| `test_packetdecoder.cpp` | PacketDecoder | 视频追帧解码、音频解码、字幕解码、错误处理 |
| `test_hexviewwidget.cpp` | HexViewWidget | 数据设置、行数计算、偏移计算 |
| `test_audiowaveformwidget.cpp` | AudioWaveformWidget | 数据设置、降采样逻辑、多声道分离 |
| `test_audiospectrogramwidget.cpp` | AudioSpectrogramWidget | FFT 计算、频谱图数据逻辑 |
| `test_mediainfowidget.cpp` | MediaInfoWidget | 格式化函数、GOP 计算、真实文件集成 |
| `test_timestampchartwidget.cpp` | TimestampChartWidget | 时间戳序列构建、异常检测 |
| `test_bitratechartwidget.cpp` | BitrateChartWidget | 码率序列构建、平均码率计算 |
| `test_avsyncchartwidget.cpp` | AVSyncChartWidget | 同步差值序列、DTS 插值 |
| `test_loganalysiswidget.cpp` | LogAnalysisWidget | 日志模型、级别过滤、回调机制 |
| `test_utils.cpp` | 通用工具函数 | 时间格式化、Hex 格式化、标志位转文字 |

**测试数据**：
- `tests/testdata/test_h264_aac.mp4`：用 FFmpeg CLI 生成的极短测试文件（< 1MB）：
  ```bash
  ffmpeg -f lavfi -i testsrc=duration=2:size=320x240:rate=25 \
         -f lavfi -i sine=frequency=440:duration=2 \
         -c:v libx264 -c:a aac -shortest tests/testdata/test_h264_aac.mp4
  ```
- `tests/testdata/test_subtitle.mkv`：含字幕的测试文件

**运行测试**：
```bash
make test    # 或 ctest --test-dir build/Debug --output-on-failure
```

---

### Phase 9：编译验证与集成调试

**目标**：确保编译通过、单元测试全部通过、基本功能可用。

1. 运行 `make debug` 编译主程序 + 测试
2. 运行 `ctest` 执行所有单元测试，确保全部 PASS
3. 修复编译错误和测试失败
4. 基本冒烟测试：打开一个 mp4 文件，查看 Packet 列表
5. 测试双击详情页
6. 测试 Hex 视图
7. 测试视频帧解码（关键帧 + P/B 帧）
8. 测试音频波形
9. 测试筛选功能

---

### Phase 10：媒体信息分析 — MediaInfoWidget

**目标**：以树形结构展示文件格式、流信息、编解码器参数等元数据，类似 flvAnalyser 的 Media Info 页面。

**文件**：`include/mediainfowidget.h` + `src/mediainfowidget.cpp`

**实现要点**：

- 使用 `QTreeWidget` 展示分组信息：Summary（概要）、Details（详细）、Video[N]、Audio[N] 等
- 各分组使用不同背景色区分（浅蓝/浅绿/浅黄/浅灰）
- 静态工具方法（公开供测试）：`formatFileSize()`、`formatDuration()`、`formatBitrate()`、`computeAverageGopSize()`、`formatAspectRatio()`
- 视频流信息：编解码器、Profile@Level、分辨率、像素格式、位深、帧率、GOP 大小、扫描类型、SAR/DAR、时间基
- 音频流信息：编解码器、采样率、声道布局、采样格式、比特率

**单元测试**（`tests/test_mediainfowidget.cpp`）：formatFileSize、formatDuration、formatBitrate、formatAspectRatio、computeAverageGopSize + 真实文件集成测试

---

### Phase 11：时间戳分析 — TimestampChartWidget

**目标**：使用 QtCharts 折线图展示视频 DTS（蓝色）和音频 DTS（橙色），检测时间戳跳变和回跳异常。

**文件**：`include/timestampchartwidget.h` + `src/timestampchartwidget.cpp`

**实现要点**：

- 使用 `QChart` + `QLineSeries` 绘制双折线图
- X 轴可切换：文件偏移 (MB) / 时间 (秒)，通过 `QComboBox` 切换
- 静态方法：`buildTimestampSeries()` 构建指定媒体类型的时间戳序列
- 静态方法：`detectAnomalies()` 检测 DTS 异常（"jump" 正向跳变 / "rollback" 回跳），使用 `TimestampAnomaly` 结构体

**单元测试**（`tests/test_timestampchartwidget.cpp`）：buildTimestampSeries（空数据、仅视频、时间模式）、detectAnomalies（正常、回跳、跳变、忽略其他类型）

---

### Phase 12：码率分析 — BitrateChartWidget

**目标**：使用 QtCharts 折线图展示逐帧视频码率。

**文件**：`include/bitratechartwidget.h` + `src/bitratechartwidget.cpp`

**实现要点**：

- 码率计算：`(size * 8) / durationTime / 1e6` Mbps
- 绿色折线图 + 红色平均码率参考线
- X 轴可切换（偏移/时间）
- 静态方法：`buildBitrateSeries()`、`computeAverageBitrate()`

**单元测试**（`tests/test_bitratechartwidget.cpp`）：buildBitrateSeries（空数据、正常、零时长、时间模式、流过滤）、computeAverageBitrate

---

### Phase 13：音视频同步分析 — AVSyncChartWidget

**目标**：使用 QtCharts 折线图展示逐包音视频同步差值 (audio DTS - video DTS) (ms)。

**文件**：`include/avsyncchartwidget.h` + `src/avsyncchartwidget.cpp`

**实现要点**：

- 按序号配对音频包和视频包，计算 delta = (audioDtsTime - videoDtsTime) * 1000 ms
- 红色折线图 + y=0 绿色参考线
- X 轴可切换（偏移/时间）
- 静态方法：`buildSyncDeltaSeries()`、`interpolateVideoDts()`（线性插值辅助函数）

**单元测试**（`tests/test_avsyncchartwidget.cpp`）：interpolateVideoDts（空、首前、末后、精确匹配、中点、四分之一点）、buildSyncDeltaSeries（空、完美同步、音频超前、偏移模式）

---

### Phase 14：日志分析 — LogAnalysisWidget

**目标**：捕获并展示 FFmpeg 运行时日志，支持级别过滤。

**文件**：`include/loganalysiswidget.h` + `src/loganalysiswidget.cpp`

**实现要点**：

- 通过 `av_log_set_callback()` 注入自定义日志回调，捕获 AV_LOG_INFO 及以上级别
- 线程安全：使用 `QMutex` + 待处理队列 + `QTimer`（200ms 轮询）将回调线程的日志传递到 UI 线程
- `LogEntry` 结构体：序号、级别、来源类名、消息、时间戳
- `LogTableModel`（QAbstractTableModel）：5 列（级别图标、序号、级别文字、摘要、详情）
- `LogFilterProxyModel`（QSortFilterProxyModel）：按最低级别过滤
- `QComboBox` 级别过滤 + 清空按钮
- 静态方法：`levelToString()`、`levelToIcon()`、`installCallback()`、`takePendingEntries()`

**单元测试**（`tests/test_loganalysiswidget.cpp`）：levelToString、levelToIcon、LogTableModel 增删改查、LogFilterProxyModel 级别过滤、widget 创建/清空

---

## 执行顺序

按依赖关系分阶段执行：

```
Phase 1: PacketReader              (数据层基础，无依赖)                    ✅ 已完成
Phase 2: PacketListModel           (依赖 Phase 1 的 PacketInfo)           ✅ 已完成
Phase 3: HexViewWidget             (独立 UI 控件，无依赖)                  ✅ 已完成
Phase 4: AudioWaveformWidget       (独立 UI 控件，无依赖)                  ✅ 已完成
Phase 5: PacketDecoder             (依赖 Phase 1 的 PacketReader)         ✅ 已完成
Phase 6: PacketDetailWidget        (依赖 Phase 3, 4, 5)                   ✅ 已完成
Phase 7: MainWindow                (依赖 Phase 1, 2, 6，整合所有组件)      ✅ 已完成
Phase 8: 单元测试基础设施           (贯穿所有 Phase)                        ✅ 已完成
Phase 9: 编译验证与集成调试         (全部完成后)                            ✅ 已完成
Phase 10: MediaInfoWidget          (依赖 Phase 1 的 StreamInfo)           ✅ 已完成
Phase 11: TimestampChartWidget     (依赖 Phase 1 的 PacketInfo + QtCharts) ✅ 已完成
Phase 12: BitrateChartWidget       (依赖 Phase 1 的 PacketInfo + QtCharts) ✅ 已完成
Phase 13: AVSyncChartWidget        (依赖 Phase 1 的 PacketInfo + QtCharts) ✅ 已完成
Phase 14: LogAnalysisWidget        (独立 FFmpeg 日志捕获)                  ✅ 已完成
```

**可并行的 Phase**：Phase 3 和 Phase 4 互相独立，可同时进行。

**测试节奏**：每完成一个 Phase 后立即编写对应的单元测试并确保通过，而不是等到最后才写。推荐顺序：
1. Phase 1 → 写 `test_packetreader.cpp` + `test_utils.cpp` → 确保通过
2. Phase 2 → 写 `test_packetlistmodel.cpp` → 确保通过
3. Phase 3 → 写 `test_hexviewwidget.cpp` → 确保通过
4. Phase 4 → 写 `test_audiowaveformwidget.cpp` → 确保通过
5. Phase 5 → 写 `test_packetdecoder.cpp` → 确保通过
6. Phase 6, 7 → 整合测试
7. Phase 8 → 确保所有测试通过
8. Phase 9 → 端到端手动测试

---

## 风险与注意事项

1. **P/B 帧追帧性能**：GOP 长度可能很大（如 250 帧），追帧解码可能需要几秒。已通过 QtConcurrent 异步解码解决 UI 冻结问题，解码期间显示“正在解码...”占位文本
2. **av_seek_frame 精度**：seek 可能不精确，需要在 seek 后循环 read_frame 并比对 pts/dts 来确认位置
3. **多次 seek 的线程安全**：`AVFormatContext` 不是线程安全的。每个 `PacketDecoder` 实例持有独立的 `AVFormatContext`，可安全在后台线程中使用。`PacketReader::readPacketData()` 使用共享 `m_formatCtx`，仅在主线程调用（Hex 懒加载时）
4. **大文件 Packet 数量**：1 小时视频可能有数十万个 Packet。QAbstractTableModel 本身可以处理，但 readAllPackets 可能需要较长时间
5. **内存管理**：所有 FFmpeg 资源（AVFormatContext, AVCodecContext, AVFrame, AVPacket, SwsContext, SwrContext）必须正确释放
6. **FFmpeg 错误处理**：所有 FFmpeg 函数调用都需检查返回值，使用 `av_strerror()` 获取描述
7. **字节序和对齐**：Hex 视图中的数据直接来自文件，无需额外处理
8. **音频格式多样性**：采样格式差异大（planar/interleaved/int16/float/double），全部交给 `swr_convert` 统一转为 float，不手动处理
9. **避免自造轮子**：任何看起来可以用库函数解决的问题，优先查找 FFmpeg/Qt/STL 中的现成函数，只在确实没有时才自己实现
10. **单元测试覆盖**：每个自写函数都有对应测试。FFmpeg 文件操作相关测试依赖测试视频文件；纯逻辑函数（格式化、计算）使用构造数据测试

---

## 库函数速查表

以下是实现中应优先使用的库函数，避免重复发明：

### FFmpeg

| 需求 | 函数 | 头文件 |
|---|---|---|
| 时间基转 double | `av_q2d(AVRational)` | `libavutil/rational.h` |
| 时间戳换算 | `av_rescale_q(ts, src_tb, dst_tb)` | `libavutil/mathematics.h` |
| 错误码转文字 | `av_strerror(errnum, buf, bufsize)` | `libavutil/error.h` |
| 编解码器名称 | `avcodec_get_name(codec_id)` | `libavcodec/avcodec.h` |
| 媒体类型名称 | `av_get_media_type_string(type)` | `libavutil/avutil.h` |
| 像素格式名称 | `av_get_pix_fmt_name(pix_fmt)` | `libavutil/pixdesc.h` |
| 采样格式名称 | `av_get_sample_fmt_name(fmt)` | `libavutil/samplefmt.h` |
| 帧内存分配 | `av_frame_get_buffer(frame, align)` | `libavutil/frame.h` |
| 像素格式转换 | `sws_scale` / `sws_scale_frame` | `libswscale/swscale.h` |
| 音频重采样 | `swr_convert` | `libswresample/swresample.h` |
| 声道布局描述 | `av_channel_layout_describe(layout, buf, bufsize)` | `libavutil/channel_layout.h` |

### Qt

| 需求 | 函数/类 | 说明 |
|---|---|---|
| 时间格式化 | `QTime::fromMSecsSinceStartOfDay(ms).toString("HH:mm:ss.zzz")` | 毫秒→时分秒 |
| Hex 格式化 | `QByteArray::toHex(' ')` | 字节→十六进制文本，空格分隔 |
| 十六进制数字 | `QString::asprintf("0x%08llX", val)` | 整数→十六进制字符串 |
| 字符可见判断 | `QChar::isPrint()` | ASCII 可打印判断 |
| 等宽字体 | `QFontDatabase::systemFont(QFontDatabase::FixedFont)` | 系统等宽字体 |
| 图像缩放 | `QPixmap::scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation)` | 保持比例缩放 |
| 路径绘制 | `QPainterPath::lineTo()` + `QPainter::drawPath()` | 高效波形绘制 |
| 排序/筛选 | `QSortFilterProxyModel` | 表格筛选 |
| 进度对话框 | `QProgressDialog` | 长时间操作进度显示 |

### C++ STL

| 需求 | 函数 | 说明 |
|---|---|---|
| 二分查找 | `std::upper_bound` / `std::lower_bound` | 关键帧索引查找 |
| 区间极值 | `std::minmax_element` | 波形降采样 |
| 排序 | `std::sort` | 如需自定义排序 |
