enable_testing()
find_package(GTest REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(FFMPEG REQUIRED)

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
    ${FFMPEG_LIBRARIES}
    -lswscale
    -lswresample
)
set_target_properties(VideoAnalyserLib PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/ui
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

# 测试数据路径
target_compile_definitions(VideoAnalyserTests PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata"
)

add_test(NAME VideoAnalyserTests COMMAND VideoAnalyserTests)

# ============================================
# 自动复制运行时 DLL 到测试目录，使 ctest 可直接运行
# ============================================
if(WIN32)
    # 复制主程序目录下由 windeployqt 部署的 Qt DLL
    add_custom_command(TARGET VideoAnalyserTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:VideoAnalyser>"
            "$<TARGET_FILE_DIR:VideoAnalyserTests>"
        COMMENT "Copying runtime DLLs to test directory..."
    )
endif()
