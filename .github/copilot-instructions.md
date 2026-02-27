# VideoAnalyser — Copilot Instructions

## Architecture

Qt 6 + FFmpeg (C++17) video packet analyzer. Data flows: **PacketReader** (FFmpeg demux) → **PacketListModel** (Qt Model/View) → **MainWindow** (tabs + table). Double-click opens **PacketDetailWidget** which uses **PacketDecoder** (static methods) for on-demand decode and embeds **HexViewWidget**, **AudioWaveformWidget**, **AudioSpectrogramWidget**, or a `ScalableImageLabel` for video frames.

- `PacketReader` owns the `AVFormatContext` and stores metadata-only `QVector<PacketInfo>`. Raw packet data is read on-demand via seek+read (`readPacketData()`).
- `PacketDecoder` is a **static-only utility class** — no instances. Each decode method opens its own `AVFormatContext` to avoid shared seek-state issues. Video P/B frames use chase decoding (seek to GOP keyframe → decode forward until PTS match, max 5000 packets guard).
- All FFmpeg objects (`AVCodecContext`, `SwsContext`, `SwrContext`, `AVFrame`, `AVPacket`) are **manually allocated/freed** — no RAII wrappers. Every error path must release resources.
- `StreamInfo::codecpar` is deep-copied via `avcodec_parameters_alloc()` + `avcodec_parameters_copy()` and freed in `PacketReader::close()`.

## Build & Run

The project uses **CMake Tools extension** in VS Code as the primary workflow. Build/run/test configuration lives in `.vscode/`.

```bash
# Build (VS Code: Ctrl+Shift+B, or CMake: Build command)
# Equivalent terminal command:
cmake --build build/Debug

# Run / Debug (VS Code: F5 launches GDB via launch.json, preLaunchTask auto-builds)

# Tests
ctest --test-dir build/Debug --output-on-failure
```

Alternative: `make debug` / `make run` via the project Makefile (local paths in `config.mk`, not committed).

- `.vscode/settings.json` configures CMake generator (Ninja), compiler paths, vcpkg toolchain — these are the actual build settings.
- `.vscode/launch.json` uses `${command:cmake.launchTargetPath}` with GDB; `preLaunchTask: "CMake: build"` ensures a fresh build before debug.
- vcpkg auto-installs deps (FFmpeg, GTest) declared in `vcpkg.json` on first configure.
- `GLOB_RECURSE` auto-collects `src/*.cpp` and `include/*.h` — no CMakeLists edit needed for new files.
- Tests link against `VideoAnalyserLib` (static lib of all src/ except main.cpp), defined in `tests/CMakeLists.txt`.

## Design Principles

### Principle 1: Prefer mature library APIs — never reinvent the wheel

Always use FFmpeg / Qt / STL APIs instead of hand-rolling equivalent logic:

| Task | DO use | DO NOT write |
|---|---|---|
| Pixel format conversion | `sws_scale` / `sws_scale_frame` | Manual YUV→RGB |
| Audio resample / format | `swr_convert` (libswresample) | Manual planar/interleaved/int16/float handling |
| Timestamp conversion | `av_q2d()`, `av_rescale_q()` | Manual time_base math |
| Binary search (keyframes) | `std::upper_bound` / `std::lower_bound` | Hand-written binary search |
| String formatting | `QString::asprintf()`, `QString::number()` | Manual hex string concatenation |
| Time formatting | `QTime::fromMSecsSinceStartOfDay().toString("HH:mm:ss.zzz")` | Manual h/m/s assembly |
| Hex formatting | `QByteArray::toHex(' ')` | Byte-by-byte conversion |
| Table sort/filter | `QSortFilterProxyModel` | Manual QVector sorting |
| Image scaling | `QPixmap::scaled(Qt::KeepAspectRatio, Qt::SmoothTransformation)` | Manual scaling |
| Waveform drawing | `QPainterPath` + `QPainter::drawPath()` | Per-pixel calculation |
| Progress dialog | `QProgressDialog` | Custom progress bar |
| Error code → text | `av_strerror()` | Manual error code map |
| Codec name | `avcodec_get_name()` | Manual codec_id→name map |
| Media type name | `av_get_media_type_string()` | Manual switch-case |

### Principle 2: Every non-trivial function must have unit tests

- Each `src/<module>.cpp` has a corresponding `tests/test_<module>.cpp`
- Coverage: normal path + boundary conditions + error handling
- FFmpeg file-dependent tests use small test videos in `tests/testdata/`
- Pure data/formatting functions test with constructed data; UI widget tests focus on data logic, not rendering

### Principle 3: Add new deps via vcpkg

When a feature needs a new library, add it to `vcpkg.json`. Current deps: `ffmpeg` (avcodec, avdevice, avfilter, avformat, swresample, swscale), `gtest`.

## Conventions

- **Member prefix**: `m_` (`m_formatCtx`, `m_packets`, `m_proxyModel`)
- **Column enums**: `Col` prefix (`ColType`, `ColIndex`, ..., `ColCount`)
- **Comments**: Chinese for inline comments and struct field docs
- **Error handling**: FFmpeg errors → `av_strerror()` → `QString`; reader returns `bool`, decoder returns empty result + optional `QString *errorMsg`
- **Static helpers exposed for testing**: `PacketListModel::formatTime()`, `AudioWaveformWidget::downsample()`, `AudioSpectrogramWidget::computeSpectrogram()`, etc. are `static`/public specifically so unit tests can call them directly.

## Testing

- Framework: **Google Test** with `QApplication` initialized in `tests/test_main.cpp`
- Test data: `tests/testdata/test_h264_aac.mp4` (320×240 H.264+AAC, <1MB). Use `TEST_DATA_DIR` macro to locate it. When the file is missing, use `GTEST_SKIP()`.
- Fixture pattern: `SetUp()` creates `PacketReader*` / model, `TearDown()` deletes; `makePacket()` factory for synthetic `PacketInfo`.
- Pure functions (formatTime, downsample, FFT helpers) are tested with constructed data; file-dependent tests use the real mp4.
- Float assertions: `EXPECT_NEAR` / `EXPECT_FLOAT_EQ`.
- **Every new non-trivial function needs a corresponding test** in `tests/test_<module>.cpp`.

## Key Files

| Path | Role |
|---|---|
| `include/packetreader.h` | `PacketInfo` / `StreamInfo` structs, `PacketReader` class |
| `include/packetdecoder.h` | Static decode API (`decodeVideoPacket`, `decodeAudioPacket`, `decodeSubtitlePacket`) |
| `include/packetlistmodel.h` | Table model + `PacketFilterProxyModel` |
| `src/mainwindow.cpp` | UI setup, file open (incl. drag-drop), tab management, filtering |
| `tests/CMakeLists.txt` | `VideoAnalyserLib` static lib target + test executable config |
