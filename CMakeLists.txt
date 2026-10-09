cmake_minimum_required(VERSION 3.20)
project(vmaf_tool LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # 源码含中文注释 (UTF-8), 避免按系统代码页解析出错
    add_compile_options(/utf-8)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# ---- FFmpeg ----
# 通过 -DFFMPEG_ROOT=C:/path/to/ffmpeg 指定 (包含 include/ 和 lib/)
find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    HINTS "${FFMPEG_ROOT}/include" "$ENV{FFMPEG_ROOT}/include")
find_library(AVFORMAT_LIB avformat HINTS "${FFMPEG_ROOT}/lib" "$ENV{FFMPEG_ROOT}/lib")
find_library(AVCODEC_LIB avcodec HINTS "${FFMPEG_ROOT}/lib" "$ENV{FFMPEG_ROOT}/lib")
find_library(AVUTIL_LIB avutil HINTS "${FFMPEG_ROOT}/lib" "$ENV{FFMPEG_ROOT}/lib")
find_library(SWSCALE_LIB swscale HINTS "${FFMPEG_ROOT}/lib" "$ENV{FFMPEG_ROOT}/lib")

if(NOT FFMPEG_INCLUDE_DIR OR NOT AVFORMAT_LIB OR NOT AVCODEC_LIB OR NOT AVUTIL_LIB OR NOT SWSCALE_LIB)
    message(FATAL_ERROR "FFmpeg not found. Set -DFFMPEG_ROOT=<path with include/ and lib/>")
endif()

# ---- libvmaf ----
# 通过 -DLIBVMAF_ROOT=C:/path/to/vmaf 指定 (包含 include/ 和 lib/)
find_path(LIBVMAF_INCLUDE_DIR libvmaf/libvmaf.h
    HINTS "${LIBVMAF_ROOT}/include" "$ENV{LIBVMAF_ROOT}/include")
find_library(VMAF_LIB vmaf HINTS "${LIBVMAF_ROOT}/lib" "$ENV{LIBVMAF_ROOT}/lib")

if(NOT LIBVMAF_INCLUDE_DIR OR NOT VMAF_LIB)
    message(FATAL_ERROR "libvmaf not found. Set -DLIBVMAF_ROOT=<path with include/ and lib/>")
endif()

set(CORE_SOURCES
    core/frame.h core/frame.cpp
    core/video_reader.h core/video_reader.cpp
    core/yuv_reader.h core/yuv_reader.cpp
    core/vmaf_evaluator.h core/vmaf_evaluator.cpp
    core/frame_aligner.h core/frame_aligner.cpp
    core/pipeline.h core/pipeline.cpp
    core/marker.h core/marker.cpp
    core/jank.h core/jank.cpp
)

# ---- third_party: 帧号 QR 标记 ----
set(QRCODEGEN_SOURCES
    third_party/qrcodegen/qrcodegen.cpp
)
set(QUIRC_SOURCES
    third_party/quirc/decode.c
    third_party/quirc/identify.c
    third_party/quirc/quirc.c
    third_party/quirc/version_db.c
)

set(GUI_SOURCES
    gui/main.cpp
    gui/main_window.h gui/main_window.cpp
    gui/eval_tab.h gui/eval_tab.cpp
    gui/capture_tab.h gui/capture_tab.cpp
    gui/jank_tab.h gui/jank_tab.cpp
    gui/preprocess_tab.h gui/preprocess_tab.cpp
    gui/worker.h gui/worker.cpp
    gui/ref_source.h gui/ref_source.cpp
    gui/vmaf_chart_widget.h gui/vmaf_chart_widget.cpp
)

add_executable(vmaf_tool WIN32 ${CORE_SOURCES} ${GUI_SOURCES} ${QRCODEGEN_SOURCES} ${QUIRC_SOURCES})

target_include_directories(vmaf_tool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIR}
    ${LIBVMAF_INCLUDE_DIR}
)

target_link_libraries(vmaf_tool PRIVATE
    Qt6::Widgets
    ${AVFORMAT_LIB}
    ${AVCODEC_LIB}
    ${AVUTIL_LIB}
    ${SWSCALE_LIB}
    ${VMAF_LIB}
)

# Release 也生成 pdb: 崩溃日志 (vmaf_tool_crash.log) 的模块+偏移可事后定位到行
target_compile_options(vmaf_tool PRIVATE /Zi)
target_link_options(vmaf_tool PRIVATE /DEBUG)

# ---- 运行时文件拷贝 ----
# FFmpeg DLL 与可执行文件 (ffmpeg/ffprobe 供预处理 Tab 内置使用)
file(GLOB FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
file(GLOB FFMPEG_EXES "${FFMPEG_ROOT}/bin/ffmpeg.exe" "${FFMPEG_ROOT}/bin/ffprobe.exe")
# libvmaf DLL 及 MinGW 运行时 (libvmaf 由 MinGW 编译)
file(GLOB LIBVMAF_DLLS "${LIBVMAF_ROOT}/bin/*.dll")
set(MINGW_BIN "" CACHE PATH "MinGW bin 目录 (含 libgcc_s_seh-1.dll 等)")
if(MINGW_BIN)
    file(GLOB MINGW_DLLS "${MINGW_BIN}/libgcc_s_seh-1.dll" "${MINGW_BIN}/libstdc++-6.dll"
         "${MINGW_BIN}/libwinpthread-1.dll")
endif()

add_custom_command(TARGET vmaf_tool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_DLLS} $<TARGET_FILE_DIR:vmaf_tool>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_EXES} $<TARGET_FILE_DIR:vmaf_tool>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBVMAF_DLLS} $<TARGET_FILE_DIR:vmaf_tool>
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/models
            $<TARGET_FILE_DIR:vmaf_tool>/models
)
if(MINGW_DLLS)
    add_custom_command(TARGET vmaf_tool POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MINGW_DLLS} $<TARGET_FILE_DIR:vmaf_tool>)
endif()

# windeployqt (自动拷贝 Qt DLL)
find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
if(WINDEPLOYQT)
    add_custom_command(TARGET vmaf_tool POST_BUILD
        COMMAND ${WINDEPLOYQT} --no-translations $<TARGET_FILE:vmaf_tool>)
endif()

# ---- 测试 ----
add_executable(marker_test
    tests/marker_test.cpp
    core/frame.cpp core/marker.cpp
    ${QRCODEGEN_SOURCES} ${QUIRC_SOURCES}
)
target_include_directories(marker_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIR}
)
target_link_libraries(marker_test PRIVATE ${AVUTIL_LIB} ${SWSCALE_LIB})

# ---- 测试: 帧号对齐 + 截图序列输入 端到端 ----
add_executable(align_test
    tests/align_test.cpp
    ${CORE_SOURCES}
    ${QRCODEGEN_SOURCES} ${QUIRC_SOURCES}
)
target_include_directories(align_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIR}
    ${LIBVMAF_INCLUDE_DIR}
)
target_link_libraries(align_test PRIVATE
    ${AVFORMAT_LIB}
    ${AVCODEC_LIB}
    ${AVUTIL_LIB}
    ${SWSCALE_LIB}
    ${VMAF_LIB}
)

# ---- 工具: 命令行 VMAF 评测 ----
add_executable(eval_cli
    tools/eval_cli.cpp
    ${CORE_SOURCES}
    ${QRCODEGEN_SOURCES} ${QUIRC_SOURCES}
)
target_include_directories(eval_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIR}
    ${LIBVMAF_INCLUDE_DIR}
)
target_link_libraries(eval_cli PRIVATE
    ${AVFORMAT_LIB}
    ${AVCODEC_LIB}
    ${AVUTIL_LIB}
    ${SWSCALE_LIB}
    ${VMAF_LIB}
)

# ---- 测试: GUI 评测收尾复现 (默认真窗口, --offscreen 无显示) ----
add_executable(eval_gui_test
    tests/eval_gui_test.cpp
    gui/main_window.h gui/main_window.cpp
    gui/eval_tab.h gui/eval_tab.cpp
    gui/capture_tab.h gui/capture_tab.cpp
    gui/jank_tab.h gui/jank_tab.cpp
    gui/preprocess_tab.h gui/preprocess_tab.cpp
    gui/worker.h gui/worker.cpp
    gui/ref_source.h gui/ref_source.cpp
    gui/vmaf_chart_widget.h gui/vmaf_chart_widget.cpp
    ${CORE_SOURCES}
    ${QRCODEGEN_SOURCES} ${QUIRC_SOURCES}
)
target_include_directories(eval_gui_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIR}
    ${LIBVMAF_INCLUDE_DIR}
)
target_link_libraries(eval_gui_test PRIVATE
    Qt6::Widgets
    ${AVFORMAT_LIB}
    ${AVCODEC_LIB}
    ${AVUTIL_LIB}
    ${SWSCALE_LIB}
    ${VMAF_LIB}
)
# 崩溃栈定位需要符号
target_compile_options(eval_gui_test PRIVATE /Zi)
target_link_options(eval_gui_test PRIVATE /DEBUG)

# ---- 工具: QR 帧号烧录 ----
add_executable(marker_burn
    tools/marker_burn.cpp
    core/frame.cpp core/marker.cpp core/video_reader.cpp core/yuv_reader.cpp
    ${QRCODEGEN_SOURCES} ${QUIRC_SOURCES}
)
target_include_directories(marker_burn PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIR}
)
target_link_libraries(marker_burn PRIVATE
    ${AVFORMAT_LIB}
    ${AVCODEC_LIB}
    ${AVUTIL_LIB}
    ${SWSCALE_LIB}
)
add_custom_command(TARGET marker_burn POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_DLLS} ${FFMPEG_EXES}
            $<TARGET_FILE_DIR:marker_burn>
)

# ---- 工具: 录屏卡顿率分析 (PerfDog 标准) ----
add_executable(jank_cli
    tools/jank_cli.cpp
    core/frame.cpp core/marker.cpp core/video_reader.cpp core/yuv_reader.cpp core/jank.cpp
    ${QRCODEGEN_SOURCES} ${QUIRC_SOURCES}
)
target_include_directories(jank_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIR}
)
target_link_libraries(jank_cli PRIVATE
    ${AVFORMAT_LIB}
    ${AVCODEC_LIB}
    ${AVUTIL_LIB}
    ${SWSCALE_LIB}
)
