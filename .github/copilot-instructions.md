# VideoAnalyser — Copilot 指令文件

## 架构

Qt 6 + FFmpeg (C++17) 视频 Packet 分析器。数据流向：**PacketReader**（FFmpeg 解封装）→ **PacketListModel**（Qt Model/View）→ **MainWindow**（标签页 + 表格）。双击打开 **PacketDetailWidget**，其中使用 **PacketDecoder**（纯静态方法）按需解码，并嵌入 **HexViewWidget**、**AudioWaveformWidget**、**AudioSpectrogramWidget** 或 `ScalableImageLabel` 显示视频帧。

- `PacketReader` 持有 `AVFormatContext`，仅存储元数据 `QVector<PacketInfo>`。原始 Packet 数据通过 seek+read 按需读取（`readPacketData()`）。
- `PacketDecoder` 是**纯静态工具类** — 不创建实例。每个解码方法独立打开自己的 `AVFormatContext`，以避免共享 seek 状态问题。视频 P/B 帧使用追帧解码（seek 到 GOP 关键帧 → 向前解码直到 PTS 匹配，最多 5000 个 Packet 的安全上限）。
- 所有 FFmpeg 对象（`AVCodecContext`、`SwsContext`、`SwrContext`、`AVFrame`、`AVPacket`）均**手动分配/释放** — 不使用 RAII 封装。每条错误路径都必须释放资源。
- `StreamInfo::codecpar` 通过 `avcodec_parameters_alloc()` + `avcodec_parameters_copy()` 深拷贝，在 `PacketReader::close()` 中释放。

## 构建与运行

项目使用 VS Code 的 **CMake Tools 扩展**作为主要工作流。构建/运行/测试配置位于 `.vscode/` 目录。

```bash
# 构建（VS Code: Ctrl+Shift+B，或 CMake: Build 命令）
# 等价终端命令：
cmake --build build/Debug

# 运行 / 调试（VS Code: F5 通过 launch.json 启动 GDB；preLaunchTask 自动构建）

# 测试
ctest --test-dir build/Debug --output-on-failure
```

替代方式：通过项目 Makefile 执行 `make debug` / `make run`（本地路径在 `config.mk` 中，未提交）。

- `.vscode/settings.json` 配置 CMake 生成器（Ninja）、编译器路径、vcpkg 工具链 — 这些是实际的构建设置。
- `.vscode/launch.json` 使用 `${command:cmake.launchTargetPath}` 配合 GDB；`preLaunchTask: "CMake: build"` 确保调试前自动构建。
- vcpkg 在首次配置时自动安装 `vcpkg.json` 中声明的依赖（FFmpeg、GTest）。
- `GLOB_RECURSE` 自动收集 `src/*.cpp` 和 `include/*.h` — 新增文件无需修改 CMakeLists.txt。
- 测试链接 `VideoAnalyserLib`（排除 main.cpp 的所有 src/ 代码编译为静态库），定义在 `tests/CMakeLists.txt` 中。

## 设计原则

### 原则一：优先使用成熟库 API — 绝不自造轮子

优先使用成熟库(ffmpeg, Qt, STL) API，不自行实现等价逻辑, 如果需要新功能，先查阅文档和社区资源，需要的第三方库通过 vcpkg 添加。当前依赖：`ffmpeg`（avcodec、avdevice、avfilter、avformat、swresample、swscale）、`gtest`。

### 原则二：所有非平凡函数必须有单元测试

- 每个 `src/<模块>.cpp` 对应一个 `tests/test_<模块>.cpp` 测试文件
- 覆盖范围：正常路径 + 边界条件 + 错误处理
- 涉及 FFmpeg 文件操作的测试使用 `tests/testdata/` 中的小型测试视频
- 纯数据/格式化函数使用构造数据测试；UI 控件测试侧重数据逻辑而非渲染

### 原则三：通过 vcpkg 添加新依赖

当功能需要新库时，添加到 `vcpkg.json`。当前依赖：`ffmpeg`（avcodec、avdevice、avfilter、avformat、swresample、swscale）、`gtest`。

## 编码规范

- **成员变量前缀**：`m_`（`m_formatCtx`、`m_packets`、`m_proxyModel`）
- **列枚举前缀**：`Col`（`ColType`、`ColIndex`、...、`ColCount`）
- **注释语言**：行内注释和结构体字段文档使用中文
- **错误处理**：FFmpeg 错误 → `av_strerror()` → `QString`；reader 返回 `bool`，decoder 返回空结果 + 可选的 `QString *errorMsg`
- **静态辅助函数暴露以供测试**：`PacketListModel::formatTime()`、`AudioWaveformWidget::downsample()`、`AudioSpectrogramWidget::computeSpectrogram()` 等声明为 `static`/public，专门用于单元测试直接调用。

## 测试

- 框架：**Google Test**，在 `tests/test_main.cpp` 中初始化 `QApplication`
- 测试数据：`tests/testdata/test_h264_aac.mp4`（320×240 H.264+AAC，<1MB）。使用 `TEST_DATA_DIR` 宏定位文件。文件不存在时使用 `GTEST_SKIP()` 跳过。
- Fixture 模式：`SetUp()` 创建 `PacketReader*` / model，`TearDown()` 删除；`makePacket()` 工厂函数生成合成的 `PacketInfo`。
- 纯函数（formatTime、downsample、FFT 辅助函数）使用构造数据测试；文件相关测试使用真实 mp4。
- 浮点断言：`EXPECT_NEAR` / `EXPECT_FLOAT_EQ`。
- **每个新增的功能函数都需要在 `tests/test_<模块>.cpp` 中添加对应测试**。

## 关键文件

| 路径 | 职责 |
|---|---|
| `include/packetreader.h` | `PacketInfo` / `StreamInfo` 结构体，`PacketReader` 类 |
| `include/packetdecoder.h` | 静态解码 API（`decodeVideoPacket`、`decodeAudioPacket`、`decodeSubtitlePacket`） |
| `include/packetlistmodel.h` | 表格模型 + `PacketFilterProxyModel` |
| `src/mainwindow.cpp` | UI 搭建、文件打开（含拖拽）、标签页管理、筛选 |
| `tests/CMakeLists.txt` | `VideoAnalyserLib` 静态库目标 + 测试可执行文件配置 |
