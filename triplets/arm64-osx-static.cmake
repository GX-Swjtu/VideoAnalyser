# 全静态 macOS ARM64 (Apple Silicon) 三元组：所有第三方库编译为静态库(.a)
# 用于生成仅依赖系统框架（Cocoa、CoreFoundation 等）的单文件 .app
#
# macOS 不支持完全静态链接（系统框架必须动态加载），
# 因此只静态链接 Qt、FFmpeg 和其他第三方库，系统框架保持动态链接。
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
