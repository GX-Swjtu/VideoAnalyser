cmake_minimum_required(VERSION 3.16)

project(VideoAnalyser VERSION 0.1 LANGUAGES CXX)

# ============================================
# 构建期信息自动检测（传递给 AboutDialog）
# ============================================
# Git 版本号：优先使用 git describe，无 tag 时 fallback 到 PROJECT_VERSION
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_RESULT
    )
endif()
if(NOT GIT_FOUND OR NOT GIT_RESULT EQUAL 0 OR GIT_VERSION STREQUAL "")
    set(GIT_VERSION "${PROJECT_VERSION}")
endif()

# 构建时间戳
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VA_ENABLE_SANITIZERS "Enable compiler sanitizers" OFF)
set(VA_SANITIZERS "address,undefined" CACHE STRING "Comma-separated sanitizers, e.g. address,undefined")
option(VA_STATIC_BUILD "全静态链接，生成单文件可执行程序（绿色版）" OFF)

# MSVC 静态构建：使用静态 CRT (/MT) 以匹配 vcpkg x64-windows-static 的库
if(VA_STATIC_BUILD AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 设置目录变量
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(UI_DIR ${CMAKE_SOURCE_DIR}/ui)
set(I18N_DIR ${CMAKE_SOURCE_DIR}/i18n)

# AUTOUIC 搜索 ui 目录
set(CMAKE_AUTOUIC_SEARCH_PATHS ${UI_DIR})

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools Concurrent Charts Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools Concurrent Charts Network)

# FFmpeg
if(VA_STATIC_BUILD)
    # 静态构建：使用 PkgConfig 解析 FFmpeg 的全部传递依赖（系统库、zlib 等）
    find_package(PkgConfig REQUIRED)
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(ENV{PKG_CONFIG_PATH} "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig")
    endif()
    pkg_check_modules(FFMPEG_PC REQUIRED
        libavdevice libavfilter libavformat libavcodec libswresample libswscale libavutil)
    set(FFMPEG_INCLUDE_DIRS ${FFMPEG_PC_INCLUDE_DIRS})
    set(FFMPEG_LIBRARY_DIRS ${FFMPEG_PC_STATIC_LIBRARY_DIRS})
    set(FFMPEG_LIBRARIES    ${FFMPEG_PC_STATIC_LIBRARIES})
    # macOS 静态 FFmpeg 依赖硬件加速框架，pkg-config 的 .pc 可能未包含
    if(APPLE)
        list(APPEND FFMPEG_LIBRARIES
            "-framework AudioToolbox"
            "-framework CoreMedia"
            "-framework VideoToolbox"
            "-framework CoreVideo"
        )
    endif()
else()
    find_package(FFMPEG REQUIRED)
    # Qt 自带的 FindFFmpeg 模块可能不将 swscale/swresample 包含在 FFMPEG_LIBRARIES 中
    # 显式查找以确保它们始终可用
    find_library(SWSCALE_LIBRARY NAMES swscale HINTS ${FFMPEG_LIBRARY_DIRS})
    find_library(SWRESAMPLE_LIBRARY NAMES swresample HINTS ${FFMPEG_LIBRARY_DIRS})
    if(SWSCALE_LIBRARY)
        list(APPEND FFMPEG_LIBRARIES ${SWSCALE_LIBRARY})
    endif()
    if(SWRESAMPLE_LIBRARY)
        list(APPEND FFMPEG_LIBRARIES ${SWRESAMPLE_LIBRARY})
    endif()
endif()

if(VA_ENABLE_SANITIZERS)
    add_library(VideoAnalyserSanitizers INTERFACE)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        string(REPLACE "," ";" _va_sanitizer_list "${VA_SANITIZERS}")
        set(_va_sanitizer_flags)
        foreach(_san IN LISTS _va_sanitizer_list)
            string(STRIP "${_san}" _san)
            if(NOT _san STREQUAL "")
                list(APPEND _va_sanitizer_flags "-fsanitize=${_san}")
            endif()
        endforeach()
        list(APPEND _va_sanitizer_flags "-fno-omit-frame-pointer")

        target_compile_options(VideoAnalyserSanitizers INTERFACE ${_va_sanitizer_flags})
        target_link_options(VideoAnalyserSanitizers INTERFACE ${_va_sanitizer_flags})
        message(STATUS "Sanitizers enabled: ${VA_SANITIZERS}")
    elseif(MSVC)
        if(VA_SANITIZERS MATCHES "address")
            target_compile_options(VideoAnalyserSanitizers INTERFACE /fsanitize=address /Zi)
            target_link_options(VideoAnalyserSanitizers INTERFACE /INCREMENTAL:NO)
            message(WARNING "MSVC currently supports AddressSanitizer only; ignoring unsupported sanitizers from VA_SANITIZERS='${VA_SANITIZERS}'")
        else()
            message(WARNING "VA_ENABLE_SANITIZERS is ON but VA_SANITIZERS='${VA_SANITIZERS}' is not supported by MSVC")
        endif()
    else()
        message(WARNING "Sanitizers requested, but compiler '${CMAKE_CXX_COMPILER_ID}' is not configured for sanitizer flags")
    endif()
endif()

set(TS_FILES ${I18N_DIR}/VideoAnalyser_zh_CN.ts)

# 自动扫描源文件、头文件和 UI 文件
file(GLOB_RECURSE SOURCES ${SRC_DIR}/*.cpp ${SRC_DIR}/*.c)
file(GLOB_RECURSE HEADERS ${INCLUDE_DIR}/*.h ${INCLUDE_DIR}/*.hpp)
file(GLOB_RECURSE UI_FILES ${UI_DIR}/*.ui)

# Qt 资源文件（图标等）
set(QRC_FILES ${CMAKE_SOURCE_DIR}/assets/resources.qrc)

# Windows 可执行文件图标
if(WIN32)
    set(WIN_RC_FILE ${CMAKE_SOURCE_DIR}/assets/app.rc)
endif()

set(PROJECT_SOURCES
        ${SOURCES}
        ${HEADERS}
        ${UI_FILES}
        ${TS_FILES}
        ${QRC_FILES}
        ${WIN_RC_FILE}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(VideoAnalyser
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    if(ANDROID)
        add_library(VideoAnalyser SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(VideoAnalyser
            ${PROJECT_SOURCES}
        )
    endif()
    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

target_include_directories(VideoAnalyser PRIVATE ${INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
target_link_directories(VideoAnalyser PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(VideoAnalyser PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Charts Qt${QT_VERSION_MAJOR}::Network ${FFMPEG_LIBRARIES})
if(TARGET VideoAnalyserSanitizers)
    target_link_libraries(VideoAnalyser PRIVATE VideoAnalyserSanitizers)
endif()

# 构建期信息宏（AboutDialog 使用）
target_compile_definitions(VideoAnalyser PRIVATE
    APP_VERSION="${GIT_VERSION}"
    APP_BUILD_TYPE="$<CONFIG>"
    APP_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
    APP_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    APP_BUILD_TIMESTAMP="${BUILD_TIMESTAMP}"
    APP_OS_NAME="${CMAKE_SYSTEM_NAME}"
    APP_OS_VERSION="${CMAKE_SYSTEM_VERSION}"
    APP_STATIC_BUILD=$<BOOL:${VA_STATIC_BUILD}>
)

# 全静态构建：体积优化（strip + gc-sections + LTO）
if(VA_STATIC_BUILD)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(APPLE)
            # macOS: Apple linker 使用 -dead_strip 消除未引用代码
            target_link_options(VideoAnalyser PRIVATE -Wl,-dead_strip)
        else()
            # Linux/MinGW: GNU ld 使用 --gc-sections 消除未引用段
            target_compile_options(VideoAnalyser PRIVATE -ffunction-sections -fdata-sections)
            target_link_options(VideoAnalyser PRIVATE -Wl,--gc-sections)
        endif()
        # LTO（跨编译单元优化，进一步消除死代码）
        set_target_properties(VideoAnalyser PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        if(MINGW)
            # MinGW：同时静态链接 CRT（-static 使 libgcc/libstdc++/libwinpthread 全部静态链接）
            target_link_options(VideoAnalyser PRIVATE -static)
        endif()
        # strip 调试符号（效果最显著，通常减 60-80%）
        add_custom_command(TARGET VideoAnalyser POST_BUILD
            COMMAND ${CMAKE_STRIP} "$<TARGET_FILE:VideoAnalyser>"
            COMMENT "Stripping debug symbols from static executable..."
        )
    elseif(MSVC)
        # MSVC：函数级链接 /Gy + 去除未引用代码 /OPT:REF + 折叠相同 COMDAT /OPT:ICF
        target_compile_options(VideoAnalyser PRIVATE /Gy)
        target_link_options(VideoAnalyser PRIVATE /OPT:REF /OPT:ICF)
        # LTO（全程序优化：编译 /GL + 链接 /LTCG）
        set_target_properties(VideoAnalyser PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        # MSVC Release 模式默认不生成调试符号 PDB，无需额外 strip
    endif()
endif()

# 平台相关的目标属性
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.VideoAnalyser)
endif()

set_target_properties(VideoAnalyser PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS VideoAnalyser
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(VideoAnalyser)
endif()

# ============================================
# 单元测试
# ============================================
enable_testing()
add_subdirectory(tests)

# ============================================
# 平台相关的构建后部署（仅动态构建）
# ============================================
if(WIN32 AND NOT VA_STATIC_BUILD)
    # Windows: 使用 windeployqt 复制 Qt DLL
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(QT_DEPLOY_TOOL)
        add_custom_command(TARGET VideoAnalyser POST_BUILD
            COMMAND "${QT_DEPLOY_TOOL}" "$<TARGET_FILE:VideoAnalyser>" --no-translations
            COMMENT "Deploying Qt runtime dependencies (Windows)..."
        )
    endif()

    # Windows: 复制 vcpkg 的 FFmpeg DLL 到构建目录
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        file(GLOB FFMPEG_DLLS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
        foreach(DLL_FILE ${FFMPEG_DLLS})
            add_custom_command(TARGET VideoAnalyser POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL_FILE}" "$<TARGET_FILE_DIR:VideoAnalyser>"
                COMMENT "Copying ${DLL_FILE}..."
            )
        endforeach()
    endif()
elseif(UNIX AND NOT APPLE AND NOT VA_STATIC_BUILD)
    # Linux: 设置 RPATH，使可执行文件能找到相对路径的共享库
    set_target_properties(VideoAnalyser PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN"
        BUILD_RPATH_USE_ORIGIN TRUE
    )
elseif(APPLE AND NOT VA_STATIC_BUILD)
    # macOS: 使用 macdeployqt 将 Qt 框架打包到 .app bundle
    find_program(QT_MAC_DEPLOY_TOOL macdeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(QT_MAC_DEPLOY_TOOL)
        add_custom_command(TARGET VideoAnalyser POST_BUILD
            COMMAND "${QT_MAC_DEPLOY_TOOL}" "$<TARGET_BUNDLE_DIR:VideoAnalyser>"
            COMMENT "Deploying Qt runtime dependencies (macOS)..."
        )
    endif()
endif()
