enable_testing()
find_package(GTest REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Concurrent Charts Network)

# FFmpeg
if(VA_STATIC_BUILD)
    # 静态构建：使用 PkgConfig 解析 FFmpeg 的全部传递依赖
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
    # Qt 自带的 FindFFmpeg 可能不包含 swscale/swresample，显式补充
    find_library(SWSCALE_LIBRARY NAMES swscale HINTS ${FFMPEG_LIBRARY_DIRS})
    find_library(SWRESAMPLE_LIBRARY NAMES swresample HINTS ${FFMPEG_LIBRARY_DIRS})
    if(SWSCALE_LIBRARY)
        list(APPEND FFMPEG_LIBRARIES ${SWSCALE_LIBRARY})
    endif()
    if(SWRESAMPLE_LIBRARY)
        list(APPEND FFMPEG_LIBRARIES ${SWRESAMPLE_LIBRARY})
    endif()
endif()

# 将业务逻辑编译为静态库供测试链接（排除 main.cpp）
file(GLOB_RECURSE LIB_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER LIB_SOURCES EXCLUDE REGEX "main\\.cpp$")
file(GLOB_RECURSE LIB_HEADERS ${CMAKE_SOURCE_DIR}/include/*.h)

add_library(VideoAnalyserLib STATIC ${LIB_SOURCES} ${LIB_HEADERS})
target_include_directories(VideoAnalyserLib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)
target_link_directories(VideoAnalyserLib PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(VideoAnalyserLib PUBLIC
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Charts
    Qt${QT_VERSION_MAJOR}::Network
    ${FFMPEG_LIBRARIES}
)
set_target_properties(VideoAnalyserLib PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/ui
)
if(TARGET VideoAnalyserSanitizers)
    target_link_libraries(VideoAnalyserLib PRIVATE VideoAnalyserSanitizers)
endif()

# 构建期信息宏（与主目标保持一致）
target_compile_definitions(VideoAnalyserLib PUBLIC
    APP_VERSION="${GIT_VERSION}"
    APP_BUILD_TYPE="$<CONFIG>"
    APP_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
    APP_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    APP_BUILD_TIMESTAMP="${BUILD_TIMESTAMP}"
    APP_OS_NAME="${CMAKE_SYSTEM_NAME}"
    APP_OS_VERSION="${CMAKE_SYSTEM_VERSION}"
    APP_STATIC_BUILD=$<BOOL:${VA_STATIC_BUILD}>
)

# 测试可执行文件
file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
add_executable(VideoAnalyserTests ${TEST_SOURCES})
target_include_directories(VideoAnalyserTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)
target_link_directories(VideoAnalyserTests PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(VideoAnalyserTests PRIVATE
    VideoAnalyserLib
    GTest::gtest
)
set_target_properties(VideoAnalyserTests PROPERTIES AUTOMOC ON)
if(TARGET VideoAnalyserSanitizers)
    target_link_libraries(VideoAnalyserTests PRIVATE VideoAnalyserSanitizers)
endif()
# 全静态构建：添加运行时静态链接标志
if(VA_STATIC_BUILD)
    if(MINGW)
        target_link_options(VideoAnalyserTests PRIVATE -static)
    endif()
endif()

# 测试数据路径
target_compile_definitions(VideoAnalyserTests PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata"
)

add_test(NAME VideoAnalyserTests COMMAND VideoAnalyserTests)

# ============================================
# 自动复制运行时 DLL 到测试目录，使 ctest 可直接运行（仅动态构建）
# ============================================
if(WIN32 AND NOT VA_STATIC_BUILD)
    # 仅复制运行时 DLL/插件，避免覆盖 tests/CTestTestfile.cmake 导致 ctest 找不到测试
    add_custom_command(TARGET VideoAnalyserTests POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR="$<TARGET_FILE_DIR:VideoAnalyser>"
            -DDEST_DIR="$<TARGET_FILE_DIR:VideoAnalyserTests>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_test_runtime.cmake"
        COMMENT "Copying runtime DLLs and Qt plugins to test directory..."
    )
endif()
