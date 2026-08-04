#include "yuv_reader.h"

#include <cstdio>

namespace vt {

YuvReader::YuvReader(std::string path, int w, int h, double fps)
    : path_(std::move(path)), w_(w), h_(h), fps_(fps > 0 ? fps : 25.0) {}

bool YuvReader::open(std::string& err) {
    if (w_ <= 0 || h_ <= 0) {
        err = "YUV 文件需要指定分辨率 (如 1920x1080)";
        return false;
    }
    fp_ = fopen(path_.c_str(), "rb");
    if (!fp_) {
        err = "无法打开文件: " + path_;
        return false;
    }
#ifdef _WIN32
    _fseeki64(fp_, 0, SEEK_END);
    const int64_t size = _ftelli64(fp_);
    _fseeki64(fp_, 0, SEEK_SET);
#else
    fseeko(fp_, 0, SEEK_END);
    const int64_t size = ftello(fp_);
    fseeko(fp_, 0, SEEK_SET);
#endif
    const int64_t frameSize = static_cast<int64_t>(w_) * h_ * 3 / 2;
    totalFrames_ = frameSize > 0 ? size / frameSize : -1;
    return true;
}

bool YuvReader::readFrame(Frame& f) {
    if (!fp_) return false;
    f.alloc(w_, h_);
    const size_t ySize = static_cast<size_t>(w_) * h_;
    const size_t cSize = ySize / 4;
    if (fread(f.y.data(), 1, ySize, fp_) != ySize) return false;
    if (fread(f.u.data(), 1, cSize, fp_) != cSize) return false;
    if (fread(f.v.data(), 1, cSize, fp_) != cSize) return false;
    f.ptsMs = static_cast<int64_t>(frameIdx_++ * 1000.0 / fps_);
    return true;
}

void YuvReader::close() {
    if (fp_) fclose(fp_);
    fp_ = nullptr;
}

std::unique_ptr<VideoReader> createYuvReader(const std::string& path, int w, int h, double fps) {
    return std::make_unique<YuvReader>(path, w, h, fps);
}

} // namespace vt
