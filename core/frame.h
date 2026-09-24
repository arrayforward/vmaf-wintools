#pragma once

#include <cstdint>
#include <vector>

namespace vt {

struct Frame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> y;
    std::vector<uint8_t> u;
    std::vector<uint8_t> v;
    int strideY = 0;
    int strideU = 0;
    int strideV = 0;
    int64_t ptsMs = 0;

    bool valid() const { return width > 0 && height > 0 && !y.empty(); }

    void alloc(int w, int h) {
        width = w;
        height = h;
        strideY = w;
        strideU = (w + 1) / 2;
        strideV = (w + 1) / 2;
        y.resize(static_cast<size_t>(strideY) * h);
        u.resize(static_cast<size_t>(strideU) * ((h + 1) / 2));
        v.resize(static_cast<size_t>(strideV) * ((h + 1) / 2));
    }
};

// 裁剪帧 (坐标会向下取整到偶数, 适配 yuv420p)
Frame cropFrame(const Frame& src, int x, int y, int w, int h);

// 帧转 RGB24 缩略图 (最长边限制为 maxDim), 返回 false 表示失败
bool frameToRgbThumb(const Frame& f, std::vector<uint8_t>& rgb, int& outW, int& outH, int maxDim = 480);

// 缩放帧 (基于 swscale)
Frame scaleFrame(const Frame& src, int w, int h);

// 旋转帧 (quarterTurns: 1=90°CW, 2=180°, 3=270°CW); 90/270 度时宽高互换
// 奇数尺寸先裁到偶数 (yuv420p 色度半分辨率对齐)
Frame rotateFrame(const Frame& src, int quarterTurns);

} // namespace vt
