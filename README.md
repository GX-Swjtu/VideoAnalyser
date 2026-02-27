# VideoAnalyser

基于 **Qt 6 + FFmpeg** 的通用视频 Packet 分析工具。打开任意格式的视频文件，遍历所有 AVPacket 并以表格形式展示，双击可查看 Packet 详情（元数据、Hex 原始数据、解码内容）。

## 功能特性

- **通用格式支持** — mp4, mkv, flv, avi, ts, mov, wmv, webm 等所有 FFmpeg 支持的封装格式
- **Packet 列表** — 表格视图展示每个 Packet 的类型、序号、偏移、大小、编解码器、时间戳、标志等
- **六大分析页面** — Data Table、Media Info、Timestamp、Bitrate、AVSync、Log 六个常驻标签页
- **媒体信息** — 树形展示文件格式、流信息、编解码器参数、GOP 大小等元数据
- **时间戳分析** — QtCharts 折线图展示视频/音频 DTS，自动检测跳变和回跳异常
- **码率分析** — 逐帧视频码率折线图，显示平均码率参考线
- **音视频同步** — 逐包 A-V sync delta 折线图，直观展示音画同步情况
- **日志分析** — 捕获 FFmpeg 运行时日志，支持级别过滤（Debug/Info/Warning/Error）
- **图表 X 轴切换** — 所有图表支持文件偏移 (MB) 与时间 (秒) 两种 X 轴模式
- **Packet 详情** — 双击打开详情标签页：元数据树形展示 + Hex 十六进制视图 + 解码内容
- **视频帧解码** — 静态图片展示，P/B 帧从关键帧追帧解码
- **音频波形** — 自定义波形控件可视化展示
- **音频频谱图** — 频谱图控件展示音频频域信息
- **拖放打开** — 支持拖放文件到窗口直接打开
- **内存友好** — 只存元数据，原始数据和解码均按需从文件读取

## 项目体检（2026-02-28）

- **测试与构建状态**：当前 `CMake: build` 和 `ctest` 通过（`VideoAnalyserTests`）。
- **逻辑问题（已修复）**：
   - 重新打开文件时，旧的 `PacketDetailWidget` 可能仍在后台解码并访问已重置的 `PacketReader`，存在并发/生命周期风险；现已在加载新文件前主动关闭旧详情窗口。
   - `loadFile()` 每次调用都会新增一次 `progressChanged` 连接，可能导致重复更新；现改为复用单连接并在重连前断开旧连接。
   - `PacketReader::open()` 中 `codecpar` 分配/拷贝未做失败处理；现已补齐错误路径并确保失败时释放资源。
- **资源泄漏检查结论**：核心 FFmpeg 对象（`AVPacket`/`AVFrame`/`AVCodecContext`/`SwsContext`/`SwrContext`）在主路径和错误路径均有对应释放，未发现确定性的内存/句柄泄漏。
- **当前已知限制**：尚未接入 AddressSanitizer/LeakSanitizer 等运行时泄漏检测，建议在 CI 增加一组带 Sanitizer 的构建配置用于长期回归。

## 环境要求

| 依赖 | 最低版本 | 说明 |
|------|---------|------|
| **CMake** | 3.16+ | 构建系统 |
| **Ninja** | — | 构建后端（推荐） |
| **Qt** | 6.x (推荐 6.8+) | UI 框架，需要 Widgets、Charts 和 LinguistTools 模块 |
| **FFmpeg** | 6.x+ | 音视频解封装和解码 |
| **vcpkg** | — | C++ 包管理器，管理 FFmpeg 和 GTest 依赖 |
| **C++ 编译器** | C++17 | MinGW 13+ / GCC 9+ / MSVC 2019+ |

## 快速开始

### 1. 克隆仓库

```bash
git clone https://github.com/<your-username>/VideoAnalyser.git
cd VideoAnalyser
```

### 2. 安装依赖

#### 安装 vcpkg（如尚未安装）

```bash
# Windows (PowerShell)
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# Linux
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
```

vcpkg 会在首次 CMake 配置时自动安装 `vcpkg.json` 中声明的依赖（FFmpeg、GTest）。

#### 安装 Qt 6

从 [Qt 官网](https://www.qt.io/download) 下载安装 Qt 6，选择与你编译器匹配的版本（如 MinGW 64-bit）。

### 3. 配置本地路径

项目提供了 **两种构建方式**，选择任意一种即可：

---

#### 方式 A：CMake Presets（推荐，适合 IDE 和命令行）

1. 复制示例文件：
   ```bash
   cp CMakeUserPresets.json.example CMakeUserPresets.json
   ```

2. 编辑 `CMakeUserPresets.json`，将路径改为你本地的实际路径：
   ```json
   {
       "version": 6,
       "configurePresets": [
           {
               "name": "windows-mingw-debug",
               "inherits": "windows-mingw-debug",
               "cacheVariables": {
                   "CMAKE_PREFIX_PATH": "你的Qt路径/6.8.3/mingw_64",
                   "CMAKE_CXX_COMPILER": "你的MinGW路径/bin/g++.exe",
                   "CMAKE_C_COMPILER": "你的MinGW路径/bin/gcc.exe",
                   "CMAKE_TOOLCHAIN_FILE": "你的vcpkg路径/scripts/buildsystems/vcpkg.cmake",
                   "VCPKG_TARGET_TRIPLET": "x64-mingw-dynamic"
               }
           }
       ]
   }
   ```

3. 构建：
   ```bash
   cmake --preset windows-mingw-debug
   cmake --build --preset windows-mingw-debug
   ```

> **VS Code 用户**：安装 CMake Tools 扩展后，会自动识别 Presets，在状态栏选择对应 Preset 即可一键构建。

---

#### 方式 B：Makefile（简洁快捷）

1. 复制示例文件：
   ```bash
   cp config.mk.example config.mk
   ```

2. 编辑 `config.mk`，填入你本地的路径：
   ```makefile
   QT_DIR       = C:/Qt/6.8.3/mingw_64
   MINGW_DIR    = C:/Qt/Tools/mingw1310_64
   VCPKG_ROOT   = C:/vcpkg
   VCPKG_TRIPLET = x64-mingw-dynamic
   ```

3. 构建和运行：
   ```bash
   make debug        # Debug 构建
   make release      # Release 构建
   make run          # 构建并运行
   ```

### 4. Linux 构建

```bash
# 安装系统依赖（Ubuntu/Debian）
sudo apt install qt6-base-dev qt6-tools-dev cmake ninja-build

# 如果使用 vcpkg 管理 FFmpeg
cp config.mk.example config.mk
# 编辑 config.mk 设置 VCPKG_ROOT
make debug

# 或者使用系统包管理器安装 FFmpeg
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libavdevice-dev libavfilter-dev
make debug
```

## 项目结构

```
├── CMakeLists.txt              # 主构建配置
├── CMakePresets.json           # CMake 预设（共享配置）
├── CMakeUserPresets.json.example # CMake 用户预设模板（本地路径）
├── Makefile                    # Make 构建入口
├── config.mk.example           # Makefile 本地配置模板
├── vcpkg.json                  # vcpkg 依赖声明
├── include/                    # 头文件
│   ├── mainwindow.h            #   主窗口，管理六大分析标签页
│   ├── packetreader.h          #   FFmpeg 解封装，遍历 Packet
│   ├── packetlistmodel.h       #   Packet 列表数据模型
│   ├── packetdetailwidget.h    #   Packet 详情标签页
│   ├── packetdecoder.h         #   解码逻辑（视频/音频/字幕）
│   ├── hexviewwidget.h         #   Hex 十六进制查看控件
│   ├── audiowaveformwidget.h   #   音频波形控件
│   ├── audiospectrogramwidget.h#   音频频谱图控件
│   ├── mediainfowidget.h       #   媒体信息分析面板
│   ├── timestampchartwidget.h  #   时间戳分析图表
│   ├── bitratechartwidget.h    #   码率分析图表
│   ├── avsyncchartwidget.h     #   音视频同步分析图表
│   └── loganalysiswidget.h     #   FFmpeg 日志分析面板
├── src/                        # 源文件
│   ├── main.cpp                #   应用入口
│   ├── mainwindow.cpp
│   ├── packetreader.cpp
│   ├── packetlistmodel.cpp
│   ├── packetdetailwidget.cpp
│   ├── packetdecoder.cpp
│   ├── hexviewwidget.cpp
│   ├── audiowaveformwidget.cpp
│   ├── audiospectrogramwidget.cpp
│   ├── mediainfowidget.cpp
│   ├── timestampchartwidget.cpp
│   ├── bitratechartwidget.cpp
│   ├── avsyncchartwidget.cpp
│   └── loganalysiswidget.cpp
├── ui/                         # Qt .ui 界面文件
│   └── mainwindow.ui
├── i18n/                       # 国际化翻译
│   └── VideoAnalyser_zh_CN.ts
└── tests/                      # 单元测试 (Google Test)
    ├── CMakeLists.txt
    ├── test_main.cpp
    ├── test_packetreader.cpp
    ├── test_packetlistmodel.cpp
    ├── test_packetdecoder.cpp
    ├── test_hexviewwidget.cpp
    ├── test_audiowaveformwidget.cpp
    ├── test_audiospectrogramwidget.cpp
    ├── test_mediainfowidget.cpp
    ├── test_timestampchartwidget.cpp
    ├── test_bitratechartwidget.cpp
    ├── test_avsyncchartwidget.cpp
    ├── test_loganalysiswidget.cpp
    ├── test_utils.cpp
    └── testdata/               # 测试用小型视频文件
```

## Make 命令参考

| 命令 | 说明 |
|------|------|
| `make` / `make debug` | Debug 构建（默认） |
| `make release` | Release 构建 |
| `make run` | 构建并运行（Debug） |
| `make run-release` | 构建并运行（Release） |
| `make clean` | 清理所有构建产物 |
| `make rebuild` | 清理后重新构建 |
| `make help` | 显示帮助信息 |

## 运行测试

```bash
# 方式 A：CMake Presets
cmake --preset windows-mingw-debug
cmake --build --preset windows-mingw-debug
ctest --preset windows-mingw-debug

# 方式 B：Makefile 构建后
cd build/Debug
ctest --output-on-failure
```

## 技术栈

- **C++17** — 编程语言
- **Qt 6 Widgets + Charts** — GUI 框架及图表组件
- **FFmpeg** (libavformat, libavcodec, libavutil, libswscale, libswresample) — 音视频处理
- **Google Test** — 单元测试框架
- **CMake + Ninja** — 构建系统
- **vcpkg** — C++ 包管理

## 许可证

[GPL-3.0](LICENSE)
