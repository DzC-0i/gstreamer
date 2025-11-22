# CMake最低版本
# 编译使用 mingw64 ：cmake -B build -G "MinGW Makefiles"       mingw32-make -j6
# 编译使用 ninja ：cmake -B build -G "Ninja"     ninja -j6  (-j6: 编译线程数目指定   -l6.0: 参数限制系统负载，避免编译时占用 100%)
# 设置编译类型为： -DCMAKE_BUILD_TYPE=Release   / -DCMAKE_BUILD_TYPE=Debug
cmake_minimum_required(VERSION 3.28.0)

project(GstreamerTutorials)

# 添加 包查找器
find_package(PkgConfig REQUIRED)

# 查找GStreamer库
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
# 查找GStreamer音频库
pkg_check_modules(GSTREAMER_AUDIO REQUIRED gstreamer-audio-1.0)
# 查找GStreamer管道工具库
pkg_check_modules(GSTREAMER_PBUTILS REQUIRED gstreamer-pbutils-1.0)
# 查找GTK3库
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# 遍历 basic-tutorials 下的所有子目录（每个子目录对应一个可执行程序）
file(GLOB SUB_DIRS1 RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/basic-tutorials ${CMAKE_CURRENT_SOURCE_DIR}/basic-tutorials/*)

foreach(SUB_DIR ${SUB_DIRS1})
    # 子目录完整路径
    set(SUB_DIR_PATH ${CMAKE_CURRENT_SOURCE_DIR}/basic-tutorials/${SUB_DIR})

    # 仅处理包含 .c 文件的目录（跳过纯 README 目录，如 basic-tutorial-10）
    file(GLOB C_FILES ${SUB_DIR_PATH}/*.c)
    if(C_FILES)
        foreach(C_FILE ${C_FILES})
            # 关键：获取 .c 文件名（不含路径、不含后缀），作为目标名
            # NAME_WE = Name Without Extension（无后缀名称）
            get_filename_component(TARGET_NAME ${C_FILE} NAME_WE)
            # 生成可执行程序：目标名 = 无后缀文件名
            add_executable(${TARGET_NAME} ${C_FILE})

            # 链接 GStreamer 依赖（头文件路径 + 库文件）
            target_include_directories(${TARGET_NAME} PUBLIC ${GSTREAMER_INCLUDE_DIRS})
            target_link_libraries(${TARGET_NAME} PUBLIC ${GSTREAMER_LIBRARIES})

            # 统一输出目录
            set_target_properties(${TARGET_NAME} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/basic-tutorials
            )

            message(STATUS "Created target: ${TARGET_NAME} → source: ${C_FILE}")
        endforeach()
    endif()
endforeach()

target_link_libraries(basic-tutorial-5 PUBLIC ${GTK3_LIBRARIES})
target_include_directories(basic-tutorial-5 PUBLIC ${GTK3_INCLUDE_DIRS})

target_link_libraries(basic-tutorial-8 PUBLIC ${GSTREAMER_AUDIO_LIBRARIES})
target_include_directories(basic-tutorial-8 PUBLIC ${GSTREAMER_AUDIO_INCLUDE_DIRS})

target_link_libraries(basic-tutorial-8_modify PUBLIC ${GSTREAMER_AUDIO_LIBRARIES})
target_include_directories(basic-tutorial-8_modify PUBLIC ${GSTREAMER_AUDIO_INCLUDE_DIRS})

target_link_libraries(basic-tutorial-9 PUBLIC ${GSTREAMER_PBUTILS_LIBRARIES})
target_include_directories(basic-tutorial-9 PUBLIC ${GSTREAMER_PBUTILS_INCLUDE_DIRS})


# 遍历 playback-tutorials 下的所有子目录（每个子目录对应一个可执行程序）
file(GLOB SUB_DIRS2 RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/playback-tutorials ${CMAKE_CURRENT_SOURCE_DIR}/playback-tutorials/*)

foreach(SUB_DIR ${SUB_DIRS2})
    # 子目录完整路径
    set(SUB_DIR_PATH ${CMAKE_CURRENT_SOURCE_DIR}/playback-tutorials/${SUB_DIR})

    # 仅处理包含 .c 文件的目录（跳过纯 README 目录，如 basic-tutorial-10）
    file(GLOB C_FILES ${SUB_DIR_PATH}/*.c)
    if(C_FILES)
        foreach(C_FILE ${C_FILES})
            # 关键：获取 .c 文件名（不含路径、不含后缀），作为目标名
            # NAME_WE = Name Without Extension（无后缀名称）
            get_filename_component(TARGET_NAME ${C_FILE} NAME_WE)
            # 生成可执行程序：目标名 = 无后缀文件名
            add_executable(${TARGET_NAME} ${C_FILE})

            # 链接 GStreamer 依赖（头文件路径 + 库文件）
            target_include_directories(${TARGET_NAME} PUBLIC ${GSTREAMER_INCLUDE_DIRS})
            target_link_libraries(${TARGET_NAME} PUBLIC ${GSTREAMER_LIBRARIES})

            # 统一输出目录
            set_target_properties(${TARGET_NAME} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/playback-tutorials
            )

            message(STATUS "Created target: ${TARGET_NAME} → source: ${C_FILE}")
        endforeach()
    endif()
endforeach()
