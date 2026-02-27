# ============================================
# VideoAnalyser 项目构建 Makefile
# ============================================

# Qt 和工具链路径
QT_DIR       = E:/Qt/6.8.3/mingw_64
MINGW_DIR    = E:/Qt/Tools/mingw1310_64
CMAKE        = E:/Qt/Tools/CMake_64/bin/cmake.exe

# 构建目录
BUILD_DEBUG   = build/Debug
BUILD_RELEASE = build/Release

# 编译器
CXX = $(MINGW_DIR)/bin/g++.exe

# CMake 公共参数
CMAKE_ARGS = -G Ninja \
	-DCMAKE_PREFIX_PATH=$(QT_DIR) \
	-DCMAKE_CXX_COMPILER=$(CXX) \
	-DCMAKE_MAKE_PROGRAM=ninja

# ---- 默认目标：Debug 构建 ----
.PHONY: all debug release clean clean-debug clean-release run run-release rebuild configure configure-release help

all: debug

# ---- Debug ----
configure:
	@echo [配置] Debug...
	@"$(CMAKE)" $(CMAKE_ARGS) -DCMAKE_BUILD_TYPE=Debug -B $(BUILD_DEBUG) -S .

debug: configure
	@echo [构建] Debug...
	@"$(CMAKE)" --build $(BUILD_DEBUG)
	@echo [完成] 输出: $(BUILD_DEBUG)/VideoAnalyser.exe

# ---- Release ----
configure-release:
	@echo [配置] Release...
	@"$(CMAKE)" $(CMAKE_ARGS) -DCMAKE_BUILD_TYPE=Release -B $(BUILD_RELEASE) -S .

release: configure-release
	@echo [构建] Release...
	@"$(CMAKE)" --build $(BUILD_RELEASE)
	@echo [完成] 输出: $(BUILD_RELEASE)/VideoAnalyser.exe

# ---- 运行 ----
run: debug
	@echo [运行] Debug...
	@$(BUILD_DEBUG)/VideoAnalyser.exe

run-release: release
	@echo [运行] Release...
	@$(BUILD_RELEASE)/VideoAnalyser.exe

# ---- 清理 ----
clean-debug:
	@echo [清理] Debug...
	@"$(CMAKE)" --build $(BUILD_DEBUG) --target clean 2>nul || rmdir /s /q $(BUILD_DEBUG) 2>nul || echo 已清理

clean-release:
	@echo [清理] Release...
	@"$(CMAKE)" --build $(BUILD_RELEASE) --target clean 2>nul || rmdir /s /q $(BUILD_RELEASE) 2>nul || echo 已清理

clean: clean-debug clean-release

# ---- 重新构建 ----
rebuild: clean debug

# ---- 帮助 ----
help:
	@echo.
	@echo  VideoAnalyser 构建命令:
	@echo  ─────────────────────────────────
	@echo   make            Debug 构建 (默认)
	@echo   make debug      Debug 构建
	@echo   make release    Release 构建
	@echo   make run        构建并运行 (Debug)
	@echo   make run-release 构建并运行 (Release)
	@echo   make clean      清理所有构建
	@echo   make rebuild    清理后重新构建
	@echo   make help       显示此帮助
	@echo.
