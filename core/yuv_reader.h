#pragma once

#include "video_reader.h"

namespace vt {

class YuvReader : public VideoReader {
public:
    YuvReader(std::string path, int w, int h, double fps);

    bool open(std::string& err) override;
    bool readFrame(Frame& f) override;
    int width() const override { return w_; }
    int height() const override { return h_; }
    double fps() const override { return fps_; }
    int64_t totalFrames() const override { return totalFrames_; }
    void close() override;

private:
    std::string path_;
    int w_, h_;
    double fps_;
    FILE* fp_ = nullptr;
    int64_t totalFrames_ = -1;
    int64_t frameIdx_ = 0;
};

} // namespace vt
