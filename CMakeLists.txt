cmake_minimum_required(VERSION 3.16)

project(VideoAnalyser VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VA_ENABLE_SANITIZERS "Enable compiler sanitizers" OFF)
set(VA_SANITIZERS "address,undefined" CACHE STRING "Comma-separated sanitizers, e.g. address,undefined")

# 设置目录变量
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(UI_DIR ${CMAKE_SOURCE_DIR}/ui)
set(I18N_DIR ${CMAKE_SOURCE_DIR}/i18n)

# AUTOUIC 搜索 ui 目录
set(CMAKE_AUTOUIC_SEARCH_PATHS ${UI_DIR})

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools Concurrent Charts)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools Concurrent Charts)

# FFmpeg
find_package(FFMPEG REQUIRED)

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

set(PROJECT_SOURCES
        ${SOURCES}
        ${HEADERS}
        ${UI_FILES}
        ${TS_FILES}
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
target_link_libraries(VideoAnalyser PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Charts ${FFMPEG_LIBRARIES} -lswscale -lswresample)
if(TARGET VideoAnalyserSanitizers)
    target_link_libraries(VideoAnalyser PRIVATE VideoAnalyserSanitizers)
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
# 平台相关的构建后部署
# ============================================
if(WIN32)
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
elseif(UNIX AND NOT APPLE)
    # Linux: 设置 RPATH，使可执行文件能找到相对路径的共享库
    set_target_properties(VideoAnalyser PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN"
        BUILD_RPATH_USE_ORIGIN TRUE
    )
endif()
