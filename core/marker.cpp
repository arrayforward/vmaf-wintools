#include "marker.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../third_party/qrcodegen/qrcodegen.hpp"

extern "C" {
#include "../third_party/quirc/quirc.h"
}

namespace vt {
namespace {

// QR 码区 21x21 模块 (version 1), 静区 4 模块, 静区外缘距帧右/底边缘 2 模块
constexpr int kQrModules = 21;
constexpr int kQuietModules = 4;
constexpr int kMarginModules = 2;
constexpr int kBlockModules = kQrModules + 2 * kQuietModules; // 29
constexpr int kTotalModules = kBlockModules + kMarginModules; // 31

constexpr uint8_t kYBlack = 16;
constexpr uint8_t kYWhite = 235;
constexpr uint8_t kChromaNeutral = 128;

// 模块尺寸 m = max(8, round(W/160)), 向下取偶数
int moduleSize(int w) {
    int m = (w + 80) / 160;
    m = std::max(8, m);
    return m & ~1;
}

// 标记块 (含静区) 在帧中的左上角, 以及码区 (不含静区) 矩形
void markerRect(int w, int h, int m, int& bx, int& by) {
    bx = w - kTotalModules * m;
    by = h - kTotalModules * m;
}

// 由 QR 实测外接框 (输入图坐标, 已含偏移/倍率修正) 与源分辨率反推视频区域
bool videoRectImpl(double qrX, double qrY, double qrSize, int srcW, int srcH, int& outX,
                   int& outY, int& outW, int& outH) {
    if (qrSize <= 0 || srcW <= 0 || srcH <= 0) return false;
    const int m = moduleSize(srcW);
    // 码区 (不含静区) 名义矩形: 左上角 (srcW-27m, srcH-27m), 边长 21m
    const double nomSize = kQrModules * m;
    const double nomX = srcW - (kBlockModules - kQuietModules + kMarginModules) * m;
    const double nomY = srcH - (kBlockModules - kQuietModules + kMarginModules) * m;
    const double k = qrSize / nomSize;
    outX = static_cast<int>(qrX - k * nomX + 0.5);
    outY = static_cast<int>(qrY - k * nomY + 0.5);
    outW = static_cast<int>(k * srcW + 0.5);
    outH = static_cast<int>(k * srcH + 0.5);
    return true;
}

// 用 quirc 解码一个窗口 (左上角在整图中的偏移 offX/offY, 坐标放大倍率 mul)
bool decodeWindow(const uint8_t* gray, int w, int h, int stride,
                  int offX, int offY, int mul, MarkerInfo& out) {
    struct quirc* q = quirc_new();
    if (!q) return false;
    bool ok = false;
    if (quirc_resize(q, w, h) == 0) {
        int qw = 0, qh = 0;
        uint8_t* buf = quirc_begin(q, &qw, &qh);
        for (int r = 0; r < qh; ++r)
            memcpy(buf + static_cast<size_t>(r) * qw, gray + static_cast<size_t>(r) * stride, qw);
        quirc_end(q);

        const int n = quirc_count(q);
        for (int i = 0; i < n && !ok; ++i) {
            struct quirc_code code;
            struct quirc_data data;
            quirc_extract(q, i, &code);
            if (quirc_decode(&code, &data) != QUIRC_SUCCESS) continue;
            // 载荷必须是 8 位纯数字帧号, 否则不是本规范的标记, 跳过
            if (data.payload_len != 8) continue;
            bool digits = true;
            for (int k = 0; k < 8; ++k)
                if (data.payload[k] < '0' || data.payload[k] > '9') { digits = false; break; }
            if (!digits) continue;

            char text[9];
            memcpy(text, data.payload, 8);
            text[8] = '\0';
            out.valid = true;
            out.frameIndex = std::atoi(text);
            // 码区实测外接框 (换回输入图坐标)
            int minX = INT_MAX, minY = INT_MAX, maxX = 0, maxY = 0;
            for (int k = 0; k < 4; ++k) {
                const int x = code.corners[k].x * mul + offX;
                const int y = code.corners[k].y * mul + offY;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
            out.qrX = minX;
            out.qrY = minY;
            out.qrSize = (maxX - minX + maxY - minY) / 2;
            ok = true;
        }
    }
    quirc_destroy(q);
    return ok;
}

// 灰度图公共解码路径: 先全图检测, 失败再按名义位置裁出右下角窗口重试;
// 仍失败则对窗口做 2x 盒式降采样再试 (椒盐噪声会耗尽 quirc 的 region/capstone
// 上限, 降采样把高频噪声平均成灰底, 8px 的标记模块缩到 4px 仍可识别)
bool decodeGray(const uint8_t* gray, int w, int h, int stride, MarkerInfo& out) {
    out = MarkerInfo{};
    if (!gray || w <= 0 || h <= 0) return false;

    if (decodeWindow(gray, w, h, stride, 0, 0, 1, out)) return true;

    const int m = moduleSize(w);
    const int side = std::min(std::min(w, h), 36 * m);
    if (side >= w && side >= h) return false; // 全图即窗口, 已试过
    const int offX = w - side, offY = h - side;
    const uint8_t* crop = gray + static_cast<size_t>(offY) * stride + offX;
    if (decodeWindow(crop, side, side, stride, offX, offY, 1, out)) return true;

    const int sw = side / 2, sh = side / 2;
    if (sw < 16 || sh < 16) return false;
    std::vector<uint8_t> small(static_cast<size_t>(sw) * sh);
    for (int r = 0; r < sh; ++r) {
        const uint8_t* s0 = crop + static_cast<size_t>(2 * r) * stride + 0;
        const uint8_t* s1 = s0 + stride;
        uint8_t* d = small.data() + static_cast<size_t>(r) * sw;
        for (int c = 0; c < sw; ++c) {
            const int sum = s0[2 * c] + s0[2 * c + 1] + s1[2 * c] + s1[2 * c + 1];
            d[c] = static_cast<uint8_t>(sum / 4);
        }
    }
    out = MarkerInfo{};
    return decodeWindow(small.data(), sw, sh, sw, offX, offY, 2, out);
}

} // namespace

bool burnMarker(Frame& f, int frameIndex) {
    if (!f.valid() || frameIndex < 0 || frameIndex > 99999999) return false;
    const int m = moduleSize(f.width);
    if (f.width < kTotalModules * m || f.height < kTotalModules * m) return false;

    char text[9];
    std::snprintf(text, sizeof(text), "%08d", frameIndex);
    // 8 位数字 -> 数字模式, 纠错 H, 自动选最小版本 (version 1, 21x21)
    const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text, qrcodegen::QrCode::Ecc::HIGH);
    const int qs = qr.getSize();
    if (qs != kQrModules) return false;

    int bx = 0, by = 0;
    markerRect(f.width, f.height, m, bx, by);

    // 先铺静区 (白), 再写码区模块
    for (int r = 0; r < kBlockModules * m; ++r) {
        uint8_t* row = f.y.data() + static_cast<size_t>(by + r) * f.strideY + bx;
        std::memset(row, kYWhite, kBlockModules * m);
    }
    const int codeOff = kQuietModules * m;
    for (int my = 0; my < qs; ++my) {
        for (int mx = 0; mx < qs; ++mx) {
            const uint8_t v = qr.getModule(mx, my) ? kYBlack : kYWhite;
            for (int dy = 0; dy < m; ++dy) {
                uint8_t* row = f.y.data() +
                    static_cast<size_t>(by + codeOff + my * m + dy) * f.strideY + bx + codeOff + mx * m;
                std::memset(row, v, m);
            }
        }
    }

    // 标记覆盖的矩形区域 U=V=128 (yuv420p 半分辨率)
    const int cx0 = bx / 2, cy0 = by / 2;
    const int cx1 = (bx + kBlockModules * m + 1) / 2, cy1 = (by + kBlockModules * m + 1) / 2;
    const int cw = f.strideU, ch = (f.height + 1) / 2;
    for (int r = cy0; r < std::min(cy1, ch); ++r) {
        const int len = std::min(cx1, cw) - cx0;
        if (len <= 0) break;
        std::memset(f.u.data() + static_cast<size_t>(r) * f.strideU + cx0, kChromaNeutral, len);
        std::memset(f.v.data() + static_cast<size_t>(r) * f.strideV + cx0, kChromaNeutral, len);
    }
    return true;
}

bool decodeMarker(const Frame& f, MarkerInfo& out) {
    if (!f.valid()) { out = MarkerInfo{}; return false; }
    return decodeGray(f.y.data(), f.width, f.height, f.strideY, out);
}

bool decodeMarkerRgb(const uint8_t* rgb, int w, int h, int stride, MarkerInfo& out) {
    out = MarkerInfo{};
    if (!rgb || w <= 0 || h <= 0 || stride < w * 3) return false;
    std::vector<uint8_t> gray(static_cast<size_t>(w) * h);
    for (int r = 0; r < h; ++r) {
        const uint8_t* src = rgb + static_cast<size_t>(r) * stride;
        uint8_t* dst = gray.data() + static_cast<size_t>(r) * w;
        for (int c = 0; c < w; ++c) {
            dst[c] = static_cast<uint8_t>((77 * src[0] + 150 * src[1] + 29 * src[2]) >> 8);
            src += 3;
        }
    }
    return decodeGray(gray.data(), w, h, w, out);
}

bool videoRectFromMarker(double qrX, double qrY, double qrSize, int srcW, int srcH, int& outX,
                         int& outY, int& outW, int& outH) {
    return videoRectImpl(qrX, qrY, qrSize, srcW, srcH, outX, outY, outW, outH);
}

} // namespace vt
