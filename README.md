# VideoAnalyser

[![CI](https://github.com/<your-username>/VideoAnalyser/actions/workflows/ci.yml/badge.svg)](https://github.com/<your-username>/VideoAnalyser/actions/workflows/ci.yml)

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

## 环境要求

| 依赖 | 最低版本 | 说明 |
|------|---------|------|
| **CMake** | 3.16+ | 构建系统 |
| **Ninja** | — | 构建后端（推荐） |
| **Qt** | 6.x (推荐 6.8+) | UI 框架，需要 Widgets、Charts、Concurrent 和 LinguistTools 模块 |
| **FFmpeg** | 6.x+ | 音视频解封装和解码 |
| **vcpkg** | — | C++ 包管理器，管理 FFmpeg 和 GTest 依赖 |
| **C++ 编译器** | C++17 | MinGW 13+ / GCC 9+ / MSVC 2019+ / Apple Clang 14+ |

## 快速开始

### 1. 克隆仓库

```bash
git clone https://github.com/<your-username>/VideoAnalyser.git
cd VideoAnalyser
```

### 2. 安装前置工具

#### 安装 vcpkg（如尚未安装）

```bash
# Windows (PowerShell)
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
# 设置环境变量（添加到系统环境变量或 PowerShell $PROFILE）
$env:VCPKG_ROOT = "C:\vcpkg"

# Linux
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT="$HOME/vcpkg"  # 添加到 ~/.bashrc 或 ~/.zshrc

# macOS
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT="$HOME/vcpkg"  # 添加到 ~/.zshrc
brew install ninja pkg-config nasm  # 系统依赖
```

vcpkg 会在首次 CMake 配置时自动安装 `vcpkg.json` 中声明的依赖（FFmpeg、GTest）。FFmpeg 首次编译耗时较长（10–30 分钟），后续会使用缓存。

#### 安装 Qt 6

从 [Qt 官网](https://www.qt.io/download) 下载安装 Qt 6，选择与你编译器匹配的版本（如 MinGW 64-bit）。安装时勾选 **Qt Charts** 附加模块。

### 3. 构建项目

项目提供了 **两种构建方式**，选择任意一种即可：

---

#### 方式 A：环境变量 + CMake Presets（推荐）

设置好环境变量后，CMakePresets.json 中的公开 preset 可直接使用，无需创建额外文件。

**Windows (MinGW)：**

```powershell
# 确保环境变量已设置（或在当前终端中临时设置）
$env:VCPKG_ROOT = "C:\vcpkg"
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.3\mingw_64"   # 你的 Qt 安装路径

# 配置、构建、测试
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

**Linux：**

```bash
export VCPKG_ROOT="$HOME/vcpkg"
export CMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64"   # 你的 Qt 安装路径

cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

**macOS：**

```bash
export VCPKG_ROOT="$HOME/vcpkg"
export CMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/macos"    # 你的 Qt 安装路径

cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
```

> 💡 **小贴士**：将 `VCPKG_ROOT` 和 `CMAKE_PREFIX_PATH` 添加到系统环境变量中，以后就不必每次手动设置。

---

#### 方式 B：CMakeUserPresets（适合需要指定编译器路径的用户）

如果你的编译器/Ninja 不在 PATH 中，或者需要锁定特定路径，可以使用 CMakeUserPresets：

1. 复制示例文件：
   ```bash
   cp CMakeUserPresets.json.example CMakeUserPresets.json
   ```

2. 编辑 `CMakeUserPresets.json`，将 `<占位符>` 替换为你本机的实际路径。

3. 构建：
   ```bash
   cmake --preset local-debug
   cmake --build --preset local-debug
   ctest --preset local-debug
   ```

> **VS Code 用户**：安装 [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools) 扩展后，会自动识别 Presets，在状态栏选择对应 Preset 即可一键构建。

### 4. Linux 额外说明

```bash
# 安装系统依赖（Ubuntu/Debian 24.04+）
sudo apt install cmake ninja-build pkg-config \
  qt6-base-dev qt6-charts-dev qt6-tools-dev qt6-l10n-tools \
  libgl1-mesa-dev libxkbcommon-dev

# 如果不使用 vcpkg 管理 FFmpeg，也可使用系统包
sudo apt install libavcodec-dev libavformat-dev libavutil-dev \
  libswscale-dev libswresample-dev libavdevice-dev libavfilter-dev
```

## 项目结构

```
├── CMakeLists.txt              # 主构建配置
├── CMakePresets.json           # CMake 预设（共享配置）
├── CMakeUserPresets.json.example # CMake 用户预设模板（本地路径）
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

## 运行测试

```bash
# Windows
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug

# Linux
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug

# macOS
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug

# 使用 CMakeUserPresets 的用户
ctest --preset local-debug
```

## 可用的 CMake Presets

| Preset | 平台 | 说明 |
|--------|------|------|
| `windows-debug` | Windows | MinGW Debug 构建 |
| `windows-release` | Windows | MinGW Release 构建 |
| `windows-asan` | Windows | MinGW Debug + AddressSanitizer |
| `windows-static` | Windows | MinGW 全静态单文件构建 |
| `windows-msvc-release` | Windows | MSVC Release 构建 |
| `windows-msvc-static` | Windows | MSVC 全静态单文件构建 |
| `linux-debug` | Linux | GCC/Clang Debug 构建 |
| `linux-release` | Linux | GCC/Clang Release 构建 |
| `linux-asan` | Linux | GCC/Clang Debug + AddressSanitizer |
| `linux-static` | Linux | GCC 全静态单文件构建 |
| `macos-debug` | macOS | Clang Debug 构建 |
| `macos-release` | macOS | Clang Release 构建 |
| `macos-asan` | macOS | Clang Debug + AddressSanitizer |
| `macos-static` | macOS | Clang 全静态构建（ARM64） |

> 以上 preset 通过 `$env{VCPKG_ROOT}` 环境变量定位 vcpkg。如需覆盖编译器等路径，创建 `CMakeUserPresets.json`（参见 `CMakeUserPresets.json.example`）。

## 技术栈

- **C++17** — 编程语言
- **Qt 6 Widgets + Charts** — GUI 框架及图表组件
- **FFmpeg** (libavformat, libavcodec, libavutil, libswscale, libswresample) — 音视频处理
- **Google Test** — 单元测试框架
- **CMake + Ninja** — 构建系统
- **vcpkg** — C++ 包管理

## 许可证

[GPL-3.0](LICENSE)
