# ============================================
# VideoAnalyser 项目构建 Makefile (跨平台)
# ============================================
# 用法：
#   1. 复制 config.mk.example 为 config.mk
#   2. 在 config.mk 中设置你本地的路径
#   3. make debug / make release / make run
# ============================================

# ---- 加载用户本地配置 ----
-include config.mk

# ---- 平台检测 ----
ifeq ($(OS),Windows_NT)
    PLATFORM     = windows
    EXE_SUFFIX   = .exe

    # Windows 默认路径（可在 config.mk 中覆盖）
    QT_DIR       ?= C:/Qt/6.8.3/mingw_64
    MINGW_DIR    ?= C:/Qt/Tools/mingw1310_64
    CMAKE        ?= cmake
    CXX          ?= $(MINGW_DIR)/bin/g++.exe
    CC           ?= $(MINGW_DIR)/bin/gcc.exe
    VCPKG_ROOT   ?= C:/vcpkg
    VCPKG_TRIPLET ?= x64-mingw-dynamic

    # 平台特定 CMake 参数
    PLATFORM_CMAKE_ARGS = \
        -DCMAKE_PREFIX_PATH=$(QT_DIR) \
        -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake \
        -DVCPKG_TARGET_TRIPLET=$(VCPKG_TRIPLET) \
        -DCMAKE_CXX_COMPILER=$(CXX) \
        -DCMAKE_C_COMPILER=$(CC)
    # 清理命令
    RM_RF = rmdir /s /q
    NULL_REDIRECT = 2>nul
else
    PLATFORM     = linux
    EXE_SUFFIX   =
    CMAKE        ?= cmake
    VCPKG_ROOT   ?= $(HOME)/vcpkg
    VCPKG_TRIPLET ?= x64-linux

    # 检查是否存在 vcpkg
    ifneq ($(wildcard $(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake),)
        PLATFORM_CMAKE_ARGS = \
            -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake \
            -DVCPKG_TARGET_TRIPLET=$(VCPKG_TRIPLET)
    else
        PLATFORM_CMAKE_ARGS =
    endif
    # 清理命令
    RM_RF = rm -rf
    NULL_REDIRECT = 2>/dev/null
endif

# 构建目录
BUILD_DEBUG   = build/Debug
BUILD_RELEASE = build/Release

# CMake 公共参数
CMAKE_ARGS = -G Ninja \
	$(PLATFORM_CMAKE_ARGS) \
	-DCMAKE_MAKE_PROGRAM=ninja

# ---- 默认目标：Debug 构建 ----
.PHONY: all debug release clean clean-debug clean-release run run-release rebuild configure configure-release help

all: debug

# ---- Debug ----
configure:
	@echo [配置] Debug $(PLATFORM)...
	@"$(CMAKE)" $(CMAKE_ARGS) -DCMAKE_BUILD_TYPE=Debug -B $(BUILD_DEBUG) -S .

debug: configure
	@echo [构建] Debug...
	@"$(CMAKE)" --build $(BUILD_DEBUG)
	@echo [完成] 输出: $(BUILD_DEBUG)/VideoAnalyser$(EXE_SUFFIX)

# ---- Release ----
configure-release:
	@echo [配置] Release $(PLATFORM)...
	@"$(CMAKE)" $(CMAKE_ARGS) -DCMAKE_BUILD_TYPE=Release -B $(BUILD_RELEASE) -S .

release: configure-release
	@echo [构建] Release...
	@"$(CMAKE)" --build $(BUILD_RELEASE)
	@echo [完成] 输出: $(BUILD_RELEASE)/VideoAnalyser$(EXE_SUFFIX)

# ---- 运行 ----
run: debug
	@echo [运行] Debug...
	@$(BUILD_DEBUG)/VideoAnalyser$(EXE_SUFFIX)

run-release: release
	@echo [运行] Release...
	@$(BUILD_RELEASE)/VideoAnalyser$(EXE_SUFFIX)

# ---- 清理 ----
clean-debug:
	@echo [清理] Debug...
	@"$(CMAKE)" --build $(BUILD_DEBUG) --target clean $(NULL_REDIRECT) || $(RM_RF) $(BUILD_DEBUG) $(NULL_REDIRECT) || echo 已清理

clean-release:
	@echo [清理] Release...
	@"$(CMAKE)" --build $(BUILD_RELEASE) --target clean $(NULL_REDIRECT) || $(RM_RF) $(BUILD_RELEASE) $(NULL_REDIRECT) || echo 已清理

clean: clean-debug clean-release

# ---- 重新构建 ----
rebuild: clean debug

# ---- 帮助 ----
help:
	@echo ""
	@echo "  VideoAnalyser 构建命令 ($(PLATFORM)):"
	@echo "  ─────────────────────────────────"
	@echo "  make              Debug 构建 (默认)"
	@echo "  make debug        Debug 构建"
	@echo "  make release      Release 构建"
	@echo "  make run          构建并运行 (Debug)"
	@echo "  make run-release  构建并运行 (Release)"
	@echo "  make clean        清理所有构建"
	@echo "  make rebuild      清理后重新构建"
	@echo "  make help         显示此帮助"
	@echo ""
