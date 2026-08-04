#pragma once

#include "frame.h"

#include <memory>
#include <string>

namespace vt {

class VideoReader {
public:
    virtual ~VideoReader() = default;
    virtual bool open(std::string& err) = 0;
    virtual bool readFrame(Frame& f) = 0; // false = EOF 或出错
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual double fps() const = 0;
    virtual int64_t totalFrames() const = 0; // 未知返回 -1
    virtual void close() = 0;
};

// 基于 FFmpeg 的容器格式读取 (mp4/mkv/...), 统一输出 yuv420p 8bit
std::unique_ptr<VideoReader> createFFmpegReader(const std::string& path);

// 原始 yuv420p 文件读取
std::unique_ptr<VideoReader> createYuvReader(const std::string& path, int w, int h, double fps);

// 根据扩展名选择读取器 (.yuv 走 YuvReader, 需要 wh 形如 "1920x1080")
std::unique_ptr<VideoReader> createReader(const std::string& path, const std::string& yuvSize,
                                          double yuvFps, std::string& err);

} // namespace vt
