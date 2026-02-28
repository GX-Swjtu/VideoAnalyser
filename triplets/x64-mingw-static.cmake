# 全静态 MinGW 三元组：所有第三方库编译为静态库(.a)
# 用于生成不依赖任何外部 DLL 的单文件绿色版可执行程序
#
# 注意：VCPKG_CRT_LINKAGE 保持 dynamic，与 vcpkg 社区 MinGW 三元组一致。
# MinGW 的 CRT 静态链接由项目 CMakeLists.txt 中的 target_link_options(-static) 控制，
# 如果在此处设为 static，vcpkg 会在编译器检测阶段注入 -static 标志导致检测失败。
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME MinGW)

# 将 PATH 传递给 vcpkg 内部的 cmake 进程，以便找到 MinGW 编译器
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED PATH)
