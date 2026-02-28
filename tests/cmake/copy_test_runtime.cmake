if(NOT DEFINED SOURCE_DIR OR NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "SOURCE_DIR and DEST_DIR are required")
endif()

file(MAKE_DIRECTORY "${DEST_DIR}")

# 复制根目录 DLL（Qt/FFmpeg/MinGW 运行时）
file(GLOB RUNTIME_DLLS "${SOURCE_DIR}/*.dll")
foreach(DLL_FILE IN LISTS RUNTIME_DLLS)
    file(COPY "${DLL_FILE}" DESTINATION "${DEST_DIR}")
endforeach()

# 复制 Qt 插件目录（windeployqt 输出）
set(PLUGIN_DIRS
    platforms
    styles
    imageformats
    iconengines
    networkinformation
    generic
    tls
)

foreach(PLUGIN_DIR IN LISTS PLUGIN_DIRS)
    if(EXISTS "${SOURCE_DIR}/${PLUGIN_DIR}")
        file(MAKE_DIRECTORY "${DEST_DIR}/${PLUGIN_DIR}")
        file(GLOB PLUGIN_DLLS "${SOURCE_DIR}/${PLUGIN_DIR}/*.dll")
        foreach(PLUGIN_DLL IN LISTS PLUGIN_DLLS)
            file(COPY "${PLUGIN_DLL}" DESTINATION "${DEST_DIR}/${PLUGIN_DIR}")
        endforeach()
    endif()
endforeach()
