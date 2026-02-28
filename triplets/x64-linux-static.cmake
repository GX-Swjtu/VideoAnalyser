# 全静态 Linux 三元组：所有第三方库编译为静态库(.a)
# 用于生成仅依赖系统库（glibc、X11、GL 等）的单文件可执行程序
#
# CRT 保持 dynamic：Linux 不建议静态链接 glibc（NSS、locale 等功能会失效），
# 因此只静态链接 Qt、FFmpeg 和其他第三方库，系统库保持动态链接。
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
