#include "frame.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>

namespace vt {

Frame cropFrame(const Frame& src, int x, int y, int w, int h) {
    Frame out;
    if (!src.valid()) return out;

    x = std::max(0, x) & ~1;
    y = std::max(0, y) & ~1;
    w = std::min(w, src.width - x) & ~1;
    h = std::min(h, src.height - y) & ~1;
    if (w <= 0 || h <= 0) return out;

    out.alloc(w, h);
    for (int r = 0; r < h; ++r)
        memcpy(out.y.data() + static_cast<size_t>(r) * out.strideY,
               src.y.data() + static_cast<size_t>(y + r) * src.strideY + x, w);

    const int cw = w / 2, ch = h / 2, cx = x / 2, cy = y / 2;
    for (int r = 0; r < ch; ++r) {
        memcpy(out.u.data() + static_cast<size_t>(r) * out.strideU,
               src.u.data() + static_cast<size_t>(cy + r) * src.strideU + cx, cw);
        memcpy(out.v.data() + static_cast<size_t>(r) * out.strideV,
               src.v.data() + static_cast<size_t>(cy + r) * src.strideV + cx, cw);
    }
    out.ptsMs = src.ptsMs;
    return out;
}

bool frameToRgbThumb(const Frame& f, std::vector<uint8_t>& rgb, int& outW, int& outH, int maxDim) {
    if (!f.valid()) return false;
    const double scale = std::min(1.0, static_cast<double>(maxDim) / std::max(f.width, f.height));
    outW = std::max(1, static_cast<int>(f.width * scale)) & ~1;
    outH = std::max(1, static_cast<int>(f.height * scale)) & ~1;
    rgb.resize(static_cast<size_t>(outW) * outH * 3);

    for (int r = 0; r < outH; ++r) {
        const int sy = std::min(f.height - 1, static_cast<int>(r / scale));
        for (int c = 0; c < outW; ++c) {
            const int sx = std::min(f.width - 1, static_cast<int>(c / scale));
            const int yy = f.y[static_cast<size_t>(sy) * f.strideY + sx];
            const int uu = f.u[static_cast<size_t>(sy / 2) * f.strideU + sx / 2];
            const int vv = f.v[static_cast<size_t>(sy / 2) * f.strideV + sx / 2];
            const int cr = vv - 128, cb = uu - 128;
            int rr = yy + static_cast<int>(1.402 * cr);
            int gg = yy - static_cast<int>(0.344136 * cb + 0.714136 * cr);
            int bb = yy + static_cast<int>(1.772 * cb);
            uint8_t* p = rgb.data() + (static_cast<size_t>(r) * outW + c) * 3;
            p[0] = static_cast<uint8_t>(std::clamp(rr, 0, 255));
            p[1] = static_cast<uint8_t>(std::clamp(gg, 0, 255));
            p[2] = static_cast<uint8_t>(std::clamp(bb, 0, 255));
        }
    }
    return true;
}

Frame scaleFrame(const Frame& src, int w, int h) {
    Frame out;
    if (!src.valid() || w <= 0 || h <= 0) return out;
    w &= ~1;
    h &= ~1;
    out.alloc(w, h);
    SwsContext* sws = sws_getContext(src.width, src.height, AV_PIX_FMT_YUV420P, w, h,
                                     AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!sws) return Frame{};
    const uint8_t* srcData[4] = {src.y.data(), src.u.data(), src.v.data(), nullptr};
    int srcStride[4] = {src.strideY, src.strideU, src.strideV, 0};
    uint8_t* dstData[4] = {out.y.data(), out.u.data(), out.v.data(), nullptr};
    int dstStride[4] = {out.strideY, out.strideU, out.strideV, 0};
    sws_scale(sws, srcData, srcStride, 0, src.height, dstData, dstStride);
    sws_freeContext(sws);
    out.ptsMs = src.ptsMs;
    return out;
}

Frame rotateFrame(const Frame& src, int quarterTurns) {
    quarterTurns &= 3;
    if (!src.valid() || quarterTurns == 0) return src;
    // 奇数尺寸裁到偶数 (yuv420p 色度半分辨率对齐)
    const int w = src.width & ~1, h = src.height & ~1;
    const bool swap = (quarterTurns & 1) != 0;
    Frame out;
    out.alloc(swap ? h : w, swap ? w : h);

    // 90°CW: (x,y)->(h-1-y, x); 180°: (x,y)->(w-1-x, h-1-y); 270°CW: (x,y)->(y, w-1-x)
    const auto rotPlane = [&](const uint8_t* sp, int ss, int pw, int ph, uint8_t* dp, int ds) {
        for (int y = 0; y < ph; ++y) {
            const uint8_t* srow = sp + static_cast<size_t>(y) * ss;
            for (int x = 0; x < pw; ++x) {
                int dx, dy;
                switch (quarterTurns) {
                case 1: dx = ph - 1 - y; dy = x; break;
                case 2: dx = pw - 1 - x; dy = ph - 1 - y; break;
                default: dx = y; dy = pw - 1 - x; break;
                }
                dp[static_cast<size_t>(dy) * ds + dx] = srow[x];
            }
        }
    };
    rotPlane(src.y.data(), src.strideY, w, h, out.y.data(), out.strideY);
    rotPlane(src.u.data(), src.strideU, w / 2, h / 2, out.u.data(), out.strideU);
    rotPlane(src.v.data(), src.strideV, w / 2, h / 2, out.v.data(), out.strideV);
    out.ptsMs = src.ptsMs;
    return out;
}

} // namespace vt
