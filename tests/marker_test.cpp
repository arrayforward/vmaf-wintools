// 帧号 QR 标记模块测试
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/frame.h"
#include "core/marker.h"

namespace {

int g_failCount = 0;
int g_passCount = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (cond) { ++g_passCount; }                                        \
        else { ++g_failCount; std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

// 简单确定性 PRNG (xorshift32)
struct Rng {
    uint32_t s = 12345;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

enum class Content { Gradient, Noise, Checker };

vt::Frame makeFrame(int w, int h, Content c, uint32_t seed = 1) {
    vt::Frame f;
    f.alloc(w, h);
    Rng rng{seed};
    for (int r = 0; r < h; ++r) {
        for (int col = 0; col < w; ++col) {
            uint8_t v = 0;
            switch (c) {
            case Content::Gradient:
                v = static_cast<uint8_t>((col * 255 / w + r * 128 / h) % 236 + 10);
                break;
            case Content::Noise:
                v = static_cast<uint8_t>(16 + rng.next() % 220);
                break;
            case Content::Checker:
                v = ((r / 40 + col / 40) % 2) ? 220 : 30;
                break;
            }
            f.y[static_cast<size_t>(r) * f.strideY + col] = v;
        }
    }
    // 色度给一点非中性内容, 确保烧录会重置它
    for (size_t i = 0; i < f.u.size(); ++i) f.u[i] = static_cast<uint8_t>(100 + (i * 7) % 57);
    for (size_t i = 0; i < f.v.size(); ++i) f.v[i] = static_cast<uint8_t>(90 + (i * 5) % 67);
    return f;
}

// 测试 1: 不同内容帧 + 不同帧号, 烧录后解码一致
void testRoundTrip() {
    const int indices[] = {0, 1, 60, 4096, 999999, 99999999};
    const Content contents[] = {Content::Gradient, Content::Noise, Content::Checker};
    for (Content c : contents) {
        vt::Frame f = makeFrame(1920, 1080, c, 42);
        for (int idx : indices) {
            vt::Frame g = f; // 拷贝
            char msg[128];
            std::snprintf(msg, sizeof(msg), "burn ok idx=%d", idx);
            CHECK(vt::burnMarker(g, idx), msg);
            vt::MarkerInfo mi;
            CHECK(vt::decodeMarker(g, mi), "decode returns true");
            std::snprintf(msg, sizeof(msg), "valid idx=%d", idx);
            CHECK(mi.valid, msg);
            std::snprintf(msg, sizeof(msg), "frameIndex %d == %d (got %d)", idx, idx, mi.frameIndex);
            CHECK(mi.frameIndex == idx, msg);
            // QR 实测外接框应位于名义码区位置 (1920 宽 -> m=12, 码区 (1596,756) 边长 252)
            const int m = 12;
            CHECK(std::abs(mi.qrX - (1920 - 27 * m)) <= 4 && std::abs(mi.qrY - (1080 - 27 * m)) <= 4 &&
                  std::abs(mi.qrSize - 21 * m) <= 4,
                  "qr box ~= nominal code rect");
            // 输入图即源帧时, 反推视频区域应大致为整帧
            int vx, vy, vw, vh;
            CHECK(vt::videoRectFromMarker(mi.qrX, mi.qrY, mi.qrSize, 1920, 1080, vx, vy, vw, vh),
                  "videoRectFromMarker ok");
            CHECK(std::abs(vx) <= 8 && std::abs(vy) <= 8 && std::abs(vw - 1920) <= 16 &&
                  std::abs(vh - 1080) <= 16,
                  "video rect ~= full frame");
        }
    }
    std::printf("test 1 (round-trip) done\n");
}

// 测试 2: 缩放到 0.5x / 0.3x 后仍能解码
void testScaled() {
    const double scales[] = {0.5, 0.3};
    for (double s : scales) {
        vt::Frame f = makeFrame(1920, 1080, Content::Gradient, 7);
        CHECK(vt::burnMarker(f, 4096), "burn for scale test");
        vt::Frame g = vt::scaleFrame(f, static_cast<int>(1920 * s), static_cast<int>(1080 * s));
        CHECK(g.valid(), "scaleFrame ok");
        vt::MarkerInfo mi;
        CHECK(vt::decodeMarker(g, mi), "decode scaled returns true");
        char msg[128];
        std::snprintf(msg, sizeof(msg), "scaled %.1fx frameIndex (got %d)", s, mi.frameIndex);
        CHECK(mi.valid && mi.frameIndex == 4096, msg);
    }
    std::printf("test 2 (scaled) done\n");
}

// 测试 3: Y 平面加小幅噪声 (+-10) 后仍能解码
void testNoise() {
    vt::Frame f = makeFrame(1920, 1080, Content::Gradient, 9);
    CHECK(vt::burnMarker(f, 60), "burn for noise test");
    Rng rng{77};
    for (auto& p : f.y) {
        const int d = static_cast<int>(rng.next() % 21) - 10;
        p = static_cast<uint8_t>(std::min(255, std::max(0, p + d)));
    }
    vt::MarkerInfo mi;
    CHECK(vt::decodeMarker(f, mi), "decode noisy returns true");
    CHECK(mi.valid && mi.frameIndex == 60, "noisy frameIndex == 60");
    std::printf("test 3 (noise) done\n");
}

// 测试 4: 无标记随机帧, decode 应返回 valid=false
void testNegative() {
    vt::Frame f = makeFrame(1920, 1080, Content::Noise, 1234);
    vt::MarkerInfo mi;
    vt::decodeMarker(f, mi);
    CHECK(!mi.valid, "no-marker frame must be invalid");
    std::printf("test 4 (negative) done\n");
}

// 测试 5: 涂抹 QR 一角, H 级纠错下仍应解码成功
// 注: version1-H 共 26 个码字, 可纠正 8 个; 连续角部涂抹超过 ~8 模块边长
// (码区面积约 15%) 即超出纠错能力, 故取 64px (~15%) 而非更大的面积
void testCorrupted() {
    vt::Frame f = makeFrame(1920, 1080, Content::Gradient, 5);
    CHECK(vt::burnMarker(f, 999999), "burn for corrupt test");
    // 码区 (version1 21x21, m=12) 位于帧右下角: 块 29*12=348px, 码区 252px
    const int m = 12;
    const int blockX = 1920 - 31 * m, blockY = 1080 - 31 * m;
    const int codeX = blockX + 4 * m, codeY = blockY + 4 * m;
    const int codeSize = 21 * m; // 252
    // 涂抹码区右下角 ~6% 面积 (避开 3 个定位角, 仅伤数据区)
    const int side = 64;
    for (int r = codeY + codeSize - side; r < codeY + codeSize; ++r) {
        for (int c = codeX + codeSize - side; c < codeX + codeSize; ++c) {
            f.y[static_cast<size_t>(r) * f.strideY + c] = 16;
        }
    }
    vt::MarkerInfo mi;
    CHECK(vt::decodeMarker(f, mi), "decode corrupted returns true");
    CHECK(mi.valid && mi.frameIndex == 999999, "corrupted frameIndex == 999999");
    std::printf("test 5 (corrupted) done\n");
}

// 测试 6: 信箱式竖屏截图 (真机场景回归): 720p 源帧缩放 540x304 后
// 居中垫黑边到 540x960, 反推视频区域应为 (0,328 540x304)
void testLetterbox() {
    vt::Frame f = makeFrame(1280, 720, Content::Gradient, 3);
    CHECK(vt::burnMarker(f, 4096), "burn for letterbox test");
    vt::Frame s = vt::scaleFrame(f, 540, 304);
    CHECK(s.valid(), "scaleFrame ok");

    vt::Frame g;
    g.alloc(540, 960);
    std::fill(g.y.begin(), g.y.end(), 16);   // 黑边
    std::fill(g.u.begin(), g.u.end(), 128);
    std::fill(g.v.begin(), g.v.end(), 128);
    const int offY = 328; // 信箱居中: (960-304)/2
    for (int r = 0; r < 304; ++r) {
        std::memcpy(g.y.data() + static_cast<size_t>(offY + r) * g.strideY,
                    s.y.data() + static_cast<size_t>(r) * s.strideY, 540);
        if (r % 2 == 0) {
            std::memcpy(g.u.data() + static_cast<size_t>((offY + r) / 2) * g.strideU,
                        s.u.data() + static_cast<size_t>(r / 2) * s.strideU, 270);
            std::memcpy(g.v.data() + static_cast<size_t>((offY + r) / 2) * g.strideV,
                        s.v.data() + static_cast<size_t>(r / 2) * s.strideV, 270);
        }
    }

    vt::MarkerInfo mi;
    CHECK(vt::decodeMarker(g, mi), "decode letterbox returns true");
    CHECK(mi.valid && mi.frameIndex == 4096, "letterbox frameIndex == 4096");

    int vx, vy, vw, vh;
    CHECK(vt::videoRectFromMarker(mi.qrX, mi.qrY, mi.qrSize, 1280, 720, vx, vy, vw, vh),
          "videoRectFromMarker ok");
    char msg[160];
    std::snprintf(msg, sizeof(msg), "letterbox video rect (got %d,%d %dx%d)", vx, vy, vw, vh);
    CHECK(std::abs(vx - 0) <= 8 && std::abs(vy - 328) <= 8 && std::abs(vw - 540) <= 12 &&
          std::abs(vh - 304) <= 12, msg);
    std::printf("test 6 (letterbox) done\n");
}

// 测试 7: 旋转 90/180/270 后仍能解码 (quirc 不受旋转影响), 且 QR 外接框中心
// 方位与旋转角度映射一致 (右下=0°, 左下=90°CW, 左上=180°, 右上=270°CW)
void testRotated() {
    vt::Frame f = makeFrame(1280, 720, Content::Gradient, 11);
    CHECK(vt::burnMarker(f, 777), "burn for rotate test");
    for (int turns = 1; turns <= 3; ++turns) {
        vt::Frame g = vt::rotateFrame(f, turns);
        CHECK(g.valid(), "rotateFrame ok");
        // 90/270 宽高互换
        CHECK(g.width == (turns == 2 ? 1280 : 720) && g.height == (turns == 2 ? 720 : 1280),
              "rotated dims swapped");
        vt::MarkerInfo mi;
        CHECK(vt::decodeMarker(g, mi), "decode rotated returns true");
        char msg[128];
        std::snprintf(msg, sizeof(msg), "rotated %dCW frameIndex (got %d)", turns * 90,
                      mi.frameIndex);
        CHECK(mi.valid && mi.frameIndex == 777, msg);
        // 方位: 中心相对帧中心的象限
        const bool right = mi.qrX + mi.qrSize / 2 >= g.width / 2;
        const bool bottom = mi.qrY + mi.qrSize / 2 >= g.height / 2;
        const int quadrant = right ? (bottom ? 0 : 3) : (bottom ? 1 : 2);
        std::snprintf(msg, sizeof(msg), "rotated %dCW quadrant %d == %d", turns * 90, quadrant,
                      turns);
        CHECK(quadrant == turns, msg);
    }
    std::printf("test 7 (rotated) done\n");
}

} // namespace

int main() {
    testRoundTrip();
    testScaled();
    testNoise();
    testNegative();
    testCorrupted();
    testLetterbox();
    testRotated();
    std::printf("----\npassed: %d, failed: %d\n", g_passCount, g_failCount);
    return g_failCount == 0 ? 0 : 1;
}
