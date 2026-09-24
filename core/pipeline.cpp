#include "pipeline.h"

#include "marker.h"
#include "video_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vt {

namespace {

void log(const PipelineCallbacks& cb, const std::string& level, const std::string& msg) {
    if (cb.onLog) cb.onLog(level, msg);
}

// 第一遍扫描: 提取全部帧的帧号标记, 并由 QR 方位投票检测画面旋转
bool scanVideo(VideoReader& reader, VideoScan& scan, std::atomic<bool>& stop,
               const PipelineCallbacks& cb, const char* tag) {
    Frame f;
    int idx = 0;
    int rotVotes[4] = {0, 0, 0, 0}; // 象限 -> 顺时针 90° 倍数: 右下=0 左下=1 左上=2 右上=3
    while (!stop.load() && reader.readFrame(f)) {
        MarkerInfo mi;
        if (decodeMarker(f, mi) && mi.valid) {
            scan.markerIndex.push_back(mi.frameIndex);
            ++scan.markerCount;
            // 累加 QR 外接框, 扫描结束取均值 (降低单帧像素量化误差)
            scan.markerQrX += mi.qrX;
            scan.markerQrY += mi.qrY;
            scan.markerQrSize += mi.qrSize;
            // 前若干帧投票旋转方向 (QR 应恒在角落, 远离中心即可靠)
            if (scan.markerCount <= 10) {
                const bool right = mi.qrX + mi.qrSize / 2 >= f.width / 2;
                const bool bottom = mi.qrY + mi.qrSize / 2 >= f.height / 2;
                ++rotVotes[right ? (bottom ? 0 : 3) : (bottom ? 1 : 2)];
            }
        } else {
            scan.markerIndex.push_back(-1);
        }
        if (++idx % 300 == 0)
            log(cb, "info", std::string(tag) + " 扫描 " + std::to_string(idx) + " 帧...");
    }
    if (scan.markerCount > 0) {
        scan.markerQrX /= scan.markerCount;
        scan.markerQrY /= scan.markerCount;
        scan.markerQrSize /= scan.markerCount;
        scan.rotation = static_cast<int>(std::max_element(rotVotes, rotVotes + 4) - rotVotes);
    }
    scan.totalFrames = static_cast<int64_t>(scan.markerIndex.size());
    scan.fps = reader.fps();
    scan.width = reader.width();
    scan.height = reader.height();
    return !stop.load();
}

struct CropRect {
    int x = 0, y = 0, w = 0, h = 0;
};

// 全平面 PSNR 结果 (dB), -1 = 无效
struct PsnrResult {
    double y = -1, u = -1, v = -1, avg = -1;
};

double planeMse(const uint8_t* pa, int strideA, const uint8_t* pb, int strideB, int w, int h) {
    double mse = 0;
    for (int r = 0; r < h; ++r) {
        const uint8_t* ra = pa + static_cast<size_t>(r) * strideA;
        const uint8_t* rb = pb + static_cast<size_t>(r) * strideB;
        for (int c = 0; c < w; ++c) {
            const int d = static_cast<int>(ra[c]) - static_cast<int>(rb[c]);
            mse += static_cast<double>(d) * d;
        }
    }
    return mse / (static_cast<double>(w) * h);
}

double mseToPsnr(double mse) {
    if (mse <= 0.0) return 99.99; // 完全相同封顶 (避免 inf, 工程惯例)
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

// Y/U/V 全平面 PSNR: 各平面独立 MSE/PSNR (MSE=0 封顶 99.99);
// avg 用各平面真实 MSE 按像素数加权 ((mse_y*wh + mse_u*wh/4 + mse_v*wh/4) / (1.5*wh)),
// 与 ffmpeg psnr 滤镜的 average 口径一致 (封顶值不参与加权)
PsnrResult computePsnr(const Frame& a, const Frame& b) {
    PsnrResult pr;
    if (!a.valid() || !b.valid() || a.width != b.width || a.height != b.height) return pr;
    const int w = a.width, h = a.height;
    const int cw = w / 2, ch = h / 2;
    const double mseY = planeMse(a.y.data(), a.strideY, b.y.data(), b.strideY, w, h);
    const double mseU = planeMse(a.u.data(), a.strideU, b.u.data(), b.strideU, cw, ch);
    const double mseV = planeMse(a.v.data(), a.strideV, b.v.data(), b.strideV, cw, ch);
    pr.y = mseToPsnr(mseY);
    pr.u = mseToPsnr(mseU);
    pr.v = mseToPsnr(mseV);
    const double mseAvg = (mseY * w * h + mseU * cw * ch + mseV * cw * ch) /
                          (static_cast<double>(w) * h + 2.0 * cw * ch);
    pr.avg = mseToPsnr(mseAvg);
    return pr;
}

// 偶数对齐 + 边界裁剪, 保证与 VMAF 初始化尺寸严格一致
CropRect normalizeCrop(CropRect r, int w, int h) {
    r.x &= ~1;
    r.y &= ~1;
    r.w &= ~1;
    r.h &= ~1;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    if (r.x > w - 2) r.x = (w - 2) & ~1;
    if (r.y > h - 2) r.y = (h - 2) & ~1;
    r.w = std::min(r.w, w - r.x) & ~1;
    r.h = std::min(r.h, h - r.y) & ~1;
    return r;
}

// 顺序解码读取指定序号的帧 (用于裁剪细化)
bool readFrameAt(const std::string& path, const std::string& yuvSize, double yuvFps, int idx,
                 Frame& out) {
    std::string err;
    auto r = createReader(path, yuvSize, yuvFps, err);
    if (!r || !r->open(err)) return false;
    Frame f;
    for (int i = 0; i <= idx; ++i) {
        if (!r->readFrame(f)) {
            r->close();
            return false;
        }
    }
    r->close();
    out = std::move(f);
    return true;
}

// 把 QR 反推的近似裁剪框吸附到真实视频区边界:
// QR 外接框有 ±1px 量化误差 (小 QR 上经缩放比放大成数 px 错位, VMAF 对错位极敏感),
// 而信箱/pillarbox 采集的黑边是纯黑, 可直接按内容边界精修。
// 仅当 approx 边距帧边 > 8px 时才吸附该边 (避免把全帧视频的黑场首行误当黑边);
// 吸附后某方向占图 >= ~97% 视为充满, 直接 snap 贴边。
CropRect refineCropByFrame(const Frame& f, CropRect a) {
    if (!f.valid()) return a;
    const int W = f.width, H = f.height;
    const int R = 24; // 吸附窗口半径 (px)
    const int x0 = std::max(0, a.x), x1 = std::min(W, a.x + a.w);
    const int y0 = std::max(0, a.y), y1 = std::min(H, a.y + a.h);
    if (x1 - x0 < 16 || y1 - y0 < 16) return a;
    // 黑行/列判定: 采样像素中 Y>48 的占比 < 0.5%
    const auto blackRow = [&](int y) {
        int bright = 0, n = 0;
        for (int x = x0; x < x1; x += 4) {
            if (f.y[static_cast<size_t>(y) * f.strideY + x] > 48) ++bright;
            ++n;
        }
        return bright * 200 < n;
    };
    const auto blackCol = [&](int x) {
        int bright = 0, n = 0;
        for (int y = y0; y < y1; y += 4) {
            if (f.y[static_cast<size_t>(y) * f.strideY + x] > 48) ++bright;
            ++n;
        }
        return bright * 200 < n;
    };
    if (a.y > 8) { // 顶边: 窗口内第一条内容行, 且窗口顶有 >=2 行黑段才吸附
        const int lo = std::max(0, a.y - R), hi = std::min(H - 1, a.y + R);
        int c = -1;
        for (int y = lo; y <= hi; ++y)
            if (!blackRow(y)) { c = y; break; }
        if (c > lo + 1) { a.h += a.y - c; a.y = c; }
    }
    if (a.y + a.h < H - 8) { // 底边
        const int lo = std::max(0, a.y + a.h - 1 - R), hi = std::min(H - 1, a.y + a.h - 1 + R);
        int c = -1;
        for (int y = hi; y >= lo; --y)
            if (!blackRow(y)) { c = y; break; }
        if (c >= 0 && hi - c > 1) a.h = c - a.y + 1;
    }
    if (a.x > 8) { // 左边
        const int lo = std::max(0, a.x - R), hi = std::min(W - 1, a.x + R);
        int c = -1;
        for (int x = lo; x <= hi; ++x)
            if (!blackCol(x)) { c = x; break; }
        if (c > lo + 1) { a.w += a.x - c; a.x = c; }
    }
    if (a.x + a.w < W - 8) { // 右边
        const int lo = std::max(0, a.x + a.w - 1 - R), hi = std::min(W - 1, a.x + a.w - 1 + R);
        int c = -1;
        for (int x = hi; x >= lo; --x)
            if (!blackCol(x)) { c = x; break; }
        if (c >= 0 && hi - c > 1) a.w = c - a.x + 1;
    }
    // 充满 snap
    if (a.x <= W / 32 && a.x + a.w >= W - W / 32) { a.x = 0; a.w = W; }
    if (a.y <= H / 32 && a.y + a.h >= H - H / 32) { a.y = 0; a.h = H; }
    return a;
}

} // namespace

void Pipeline::run(const PipelineConfig& cfg, const PipelineCallbacks& cb) {
    Summary summary;

    // ---------- 第一遍: 扫描两端视频 ----------
    VideoScan scanRef, scanDist;
    bool distSampling = false; // 目标为截图采样序列 (image2)
    {
        std::string err;
        auto reader = createReader(cfg.refPath, cfg.yuvRefSize, cfg.yuvFps, err);
        if (!reader || !reader->open(err)) {
            summary.error = "参考视频: " + err;
            if (cb.onFinished) cb.onFinished(summary);
            return;
        }
        log(cb, "info", "参考视频: " + std::to_string(reader->width()) + "x" +
                            std::to_string(reader->height()) + " @ " +
                            std::to_string(reader->fps()).substr(0, 5) + "fps");
        if (!scanVideo(*reader, scanRef, stopRequested, cb, "[参考]")) {
            summary.error = "已取消";
            if (cb.onFinished) cb.onFinished(summary);
            return;
        }
        reader->close();
    }
    {
        std::string err;
        auto reader = createReader(cfg.distPath, cfg.yuvDistSize, cfg.yuvFps, err);
        if (!reader || !reader->open(err)) {
            summary.error = "目标视频: " + err;
            if (cb.onFinished) cb.onFinished(summary);
            return;
        }
        log(cb, "info", "目标视频: " + std::to_string(reader->width()) + "x" +
                            std::to_string(reader->height()) + " @ " +
                            std::to_string(reader->fps()).substr(0, 5) + "fps");
        distSampling = reader->isImageSequence();
        if (!scanVideo(*reader, scanDist, stopRequested, cb, "[目标]")) {
            summary.error = "已取消";
            if (cb.onFinished) cb.onFinished(summary);
            return;
        }
        reader->close();
    }

    log(cb, "info", "扫描完成: 参考 " + std::to_string(scanRef.totalFrames) + " 帧, 目标 " +
                        std::to_string(scanDist.totalFrames) + " 帧");

    // ---------- 对齐 (QR 帧号精确匹配) ----------
    const double refMarkerRate = scanRef.totalFrames > 0
                                     ? static_cast<double>(scanRef.markerCount) / scanRef.totalFrames
                                     : 0.0;
    const double distMarkerRate =
        scanDist.totalFrames > 0
            ? static_cast<double>(scanDist.markerCount) / scanDist.totalFrames
            : 0.0;
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "帧号识别率: 参考 %d/%lld (%.1f%%), 目标 %d/%lld (%.1f%%)",
                 scanRef.markerCount, static_cast<long long>(scanRef.totalFrames),
                 refMarkerRate * 100.0, scanDist.markerCount,
                 static_cast<long long>(scanDist.totalFrames), distMarkerRate * 100.0);
        log(cb, "info", buf);
    }
    summary.markerUnrecognized = static_cast<int>(scanRef.totalFrames - scanRef.markerCount) +
                                 static_cast<int>(scanDist.totalFrames - scanDist.markerCount);
    summary.markerCoverage = refMarkerRate;

    if (refMarkerRate < 0.8 || distMarkerRate < 0.8) {
        summary.error = "未检测到帧号标记 (识别率需两端均 >= 80%), 请使用 marker_burn 生成片源";
        if (cb.onFinished) cb.onFinished(summary);
        return;
    }
    log(cb, "info", "两端帧号识别率均 >= 80%, 使用帧号对齐");
    if (distSampling)
        log(cb, "info", "目标为截图采样序列, 仅统计匹配帧对 (未采到的参考帧不计丢帧)");
    FrameMapping mapping = FrameAligner::buildMarkerMapping(scanRef, scanDist);

    int matched = 0;
    for (int d : mapping.refToDist)
        if (d >= 0) ++matched;
    // 采样模式下未匹配的参考帧只是未采样, 不计丢帧
    summary.dropped = distSampling ? 0 : mapping.droppedCount;
    summary.frozen = mapping.freezeCount;
    summary.matched = matched;
    summary.freezeSegments = mapping.freezeSegments;
    summary.freezeMaxLen = mapping.freezeMaxLen;
    if (distSampling)
        log(cb, "info", "帧映射: 匹配 " + std::to_string(matched) + " 对, 未采样 " +
                            std::to_string(mapping.droppedCount) + ", 冻结/重复目标帧 " +
                            std::to_string(mapping.freezeCount));
    else
        log(cb, "info", "帧映射: 匹配 " + std::to_string(matched) + " 对, 丢帧 " +
                            std::to_string(mapping.droppedCount) + ", 冻结/重复目标帧 " +
                            std::to_string(mapping.freezeCount));

    if (matched == 0) {
        summary.error = "没有匹配到任何帧对, 无法评估 (两端片源帧号无交集)";
        if (cb.onFinished) cb.onFinished(summary);
        return;
    }

    // ---------- 裁剪 (默认全帧; 由 QR 实测外接框反推各端视频区域) ----------
    // 目标侧画面旋转: distFix = 转正所需的顺时针 90° 倍数; distW/distH 为转正后尺寸
    const int distFix = (4 - scanDist.rotation) % 4;
    const int distW = (scanDist.rotation & 1) ? scanDist.height : scanDist.width;
    const int distH = (scanDist.rotation & 1) ? scanDist.width : scanDist.height;
    if (scanDist.rotation != 0)
        log(cb, "info", "检测到目标画面旋转 " + std::to_string(scanDist.rotation * 90) +
                            "° (顺时针), 已自动转正");

    // 第一个映射对 (供旋转坐标系修正与黑边细化)
    int firstRef = -1, firstDist = -1;
    for (size_t i = 0; i < mapping.refToDist.size(); ++i) {
        if (mapping.refToDist[i] >= 0) {
            firstRef = static_cast<int>(i);
            firstDist = mapping.refToDist[i];
            break;
        }
    }

    CropRect rc = normalizeCrop(CropRect{0, 0, scanRef.width, scanRef.height}, scanRef.width,
                                scanRef.height);
    CropRect dc = normalizeCrop(CropRect{0, 0, distW, distH}, distW, distH);
    {
        // 目标可能是缩放/信箱化的采集帧, 反推的目标分辨率取参考视频
        // 反推结果先按最近偶数取整 (normalizeCrop 的 &~1 只会向下取, 1px 反推误差
        // 落在奇数上会被放大成 2px 错位, VMAF 对错位的惩罚远大于缩放)
        const auto roundEven = [](int v) { return 2 * ((v + 1) / 2); };
        int vx, vy, vw, vh;
        if (scanRef.markerQrSize > 0 &&
            videoRectFromMarker(scanRef.markerQrX, scanRef.markerQrY, scanRef.markerQrSize,
                                scanRef.width, scanRef.height, vx, vy, vw, vh)) {
            rc = normalizeCrop(CropRect{roundEven(vx), roundEven(vy), roundEven(vw),
                                        roundEven(vh)},
                               scanRef.width, scanRef.height);
            log(cb, "info", "自动裁剪(参考): 帧号标记反推视频区域 " + std::to_string(rc.w) + "x" +
                                std::to_string(rc.h) + " @(" + std::to_string(rc.x) + "," +
                                std::to_string(rc.y) + ")");
        }
        if (scanDist.markerQrSize > 0) {
            double qx = scanDist.markerQrX, qy = scanDist.markerQrY, qs = scanDist.markerQrSize;
            if (distFix != 0) {
                // 扫描存的 QR 框是旋转前坐标系: 转正第一个映射帧后重新解码拿框
                Frame f;
                if (firstDist >= 0 &&
                    readFrameAt(cfg.distPath, cfg.yuvDistSize, cfg.yuvFps, firstDist, f)) {
                    f = rotateFrame(f, distFix);
                    MarkerInfo mi;
                    if (decodeMarker(f, mi) && mi.valid) {
                        qx = mi.qrX;
                        qy = mi.qrY;
                        qs = mi.qrSize;
                    }
                }
            }
            if (videoRectFromMarker(qx, qy, qs, scanRef.width, scanRef.height, vx, vy, vw, vh)) {
                dc = normalizeCrop(CropRect{roundEven(vx), roundEven(vy), roundEven(vw),
                                            roundEven(vh)},
                                   distW, distH);
                log(cb, "info", "自动裁剪(目标): 帧号标记反推视频区域 " + std::to_string(dc.w) +
                                    "x" + std::to_string(dc.h) + " @(" + std::to_string(dc.x) +
                                    "," + std::to_string(dc.y) + ")");
            }
        }
    }
    {
        // 黑边/充满细化: 用第一个映射帧的实际内容把近似裁剪框吸附到视频区边界
        Frame f;
        if (scanRef.markerQrSize > 0 && firstRef >= 0 &&
            readFrameAt(cfg.refPath, cfg.yuvRefSize, cfg.yuvFps, firstRef, f)) {
            const CropRect refined = normalizeCrop(refineCropByFrame(f, rc), scanRef.width,
                                                   scanRef.height);
            if (refined.x != rc.x || refined.y != rc.y || refined.w != rc.w || refined.h != rc.h) {
                rc = refined;
                log(cb, "info", "裁剪细化(参考): 黑边吸附/充满 -> " + std::to_string(rc.w) + "x" +
                                    std::to_string(rc.h) + " @(" + std::to_string(rc.x) + "," +
                                    std::to_string(rc.y) + ")");
            }
        }
        if (scanDist.markerQrSize > 0 && firstDist >= 0 &&
            readFrameAt(cfg.distPath, cfg.yuvDistSize, cfg.yuvFps, firstDist, f)) {
            if (distFix != 0) f = rotateFrame(f, distFix); // 转正后再做边界吸附
            const CropRect refined =
                normalizeCrop(refineCropByFrame(f, dc), distW, distH);
            if (refined.x != dc.x || refined.y != dc.y || refined.w != dc.w || refined.h != dc.h) {
                dc = refined;
                log(cb, "info", "裁剪细化(目标): 黑边吸附/充满 -> " + std::to_string(dc.w) + "x" +
                                    std::to_string(dc.h) + " @(" + std::to_string(dc.x) + "," +
                                    std::to_string(dc.y) + ")");
            }
        }
    }
    if (rc.w <= 0 || rc.h <= 0 || dc.w <= 0 || dc.h <= 0) {
        summary.error = "裁剪区域无效 (超出画面范围)";
        if (cb.onFinished) cb.onFinished(summary);
        return;
    }
    log(cb, "info", "裁剪: 参考 " + std::to_string(rc.w) + "x" + std::to_string(rc.h) + " @(" +
                        std::to_string(rc.x) + "," + std::to_string(rc.y) + "), 目标 " +
                        std::to_string(dc.w) + "x" + std::to_string(dc.h) + " @(" +
                        std::to_string(dc.x) + "," + std::to_string(dc.y) + ")");

    // ---------- 统一比较分辨率 ----------
    // 两端裁剪尺寸不一致时取较小者, 把较大端缩下来比较, 而不是把小尺寸采集帧
    // 上采样回源分辨率 (纯重采样往返 PSNR 上限 ~32dB, 会把无损链路的 VMAF 压到 ~40)。
    // 注: libvmaf v0.6.1 模型按 1080p 观看条件校准, 小分辨率下分数语义有偏差, 可接受
    const bool refSmaller =
        static_cast<int64_t>(rc.w) * rc.h <= static_cast<int64_t>(dc.w) * dc.h;
    const int cmpW = (refSmaller ? rc.w : dc.w) & ~1;
    const int cmpH = (refSmaller ? rc.h : dc.h) & ~1;
    if (cmpW != rc.w || cmpH != rc.h || cmpW != dc.w || cmpH != dc.h)
        log(cb, "info", "比较分辨率: " + std::to_string(cmpW) + "x" + std::to_string(cmpH) +
                            " (取两端裁剪的较小者, 较大端降采样)");

    // ---------- 初始化 VMAF ----------
    VmafEvaluator vmaf;
    {
        std::string err;
        if (!vmaf.init(cfg.vmaf, cmpW, cmpH, err)) {
            summary.error = err;
            if (cb.onFinished) cb.onFinished(summary);
            return;
        }
    }

    // ---------- 第二遍: 按映射送帧 ----------
    std::string err;
    auto refReader = createReader(cfg.refPath, cfg.yuvRefSize, cfg.yuvFps, err);
    auto distReader = createReader(cfg.distPath, cfg.yuvDistSize, cfg.yuvFps, err);
    if (!refReader->open(err) || !distReader->open(err)) {
        summary.error = err;
        if (cb.onFinished) cb.onFinished(summary);
        return;
    }

    const int totalRef = static_cast<int>(mapping.refToDist.size());
    // 采样模式下进度按匹配对数走, 未采样的参考帧不占进度
    const int progressTotal = distSampling ? matched : totalRef;
    int progressDone = 0;
    int distWanted = mapping.distStart;
    Frame rf, df;
    int pushed = 0;
    std::vector<int> pushOrder; // pushOrder[i] = refToDist 下标

    // 跳过对齐起点之前的帧
    for (int i = 0; i < mapping.refStart; ++i)
        if (!refReader->readFrame(rf)) break;
    for (int i = 0; i < mapping.distStart; ++i)
        if (!distReader->readFrame(df)) break;

    for (int r = 0; r < totalRef && !stopRequested.load(); ++r) {
        const int dIdx = mapping.refToDist[static_cast<size_t>(r)];

        // 每个参考帧都必须读取, 保持解码顺序
        if (!refReader->readFrame(rf)) break;

        if (dIdx < 0) {
            if (distSampling) continue; // 未采样, 不是丢帧
            FrameResult fr;
            fr.refIdx = r + mapping.refStart;
            fr.distIdx = dIdx;
            fr.dropped = true;
            summary.frames.push_back(fr);
            if (cb.onFrame) cb.onFrame(fr);
            if (cb.onProgress) cb.onProgress(r + 1, totalRef);
            log(cb, "warn", "帧 " + std::to_string(fr.refIdx) + " 丢帧");
            continue;
        }

        FrameResult fr;
        fr.refIdx = r + mapping.refStart;
        fr.distIdx = dIdx;

        // 目标帧号乱序 (采集乱序/抖动): 顺序 reader 回退不了, 重开目标输入回读
        // (marker 映射 distStart 恒为 0; 采样序列通常很小, 重开代价可接受)
        if (dIdx < distWanted) {
            log(cb, "warn", "目标帧 " + std::to_string(dIdx) + " 乱序, 重开目标输入回读");
            distReader->close();
            distReader = createReader(cfg.distPath, cfg.yuvDistSize, cfg.yuvFps, err);
            if (!distReader || !distReader->open(err)) {
                summary.error = "目标视频: " + err;
                break;
            }
            distWanted = mapping.distStart;
        }

        // 顺序解码目标到 dIdx
        bool gotDist = false;
        while (distWanted <= dIdx) {
            if (!distReader->readFrame(df)) {
                distWanted = dIdx + 1;
                break;
            }
            if (distWanted == dIdx) gotDist = true;
            ++distWanted;
        }
        if (!gotDist) {
            if (distSampling) continue; // 采样序列取不到该帧, 视为未采样
            fr.dropped = true;
            summary.frames.push_back(fr);
            if (cb.onFrame) cb.onFrame(fr);
            continue;
        }

        // 画面旋转转正 (录屏被旋转时), 转正后再走裁剪/缩放/比对
        if (distFix) df = rotateFrame(df, distFix);

        Frame rc2 = cropFrame(rf, rc.x, rc.y, rc.w, rc.h);
        Frame dc2 = cropFrame(df, dc.x, dc.y, dc.w, dc.h);
        // 较大端降采样到共同比较分辨率 (同尺寸时零开销)
        if (rc2.width != cmpW || rc2.height != cmpH)
            rc2 = scaleFrame(rc2, cmpW, cmpH);
        if (dc2.width != cmpW || dc2.height != cmpH)
            dc2 = scaleFrame(dc2, cmpW, cmpH);

        // PSNR 与喂给 VMAF 的帧完全一致 (裁剪 + 统一比较分辨率之后)
        const PsnrResult pr = computePsnr(rc2, dc2);
        fr.psnr = pr.avg;
        fr.psnrY = pr.y;
        fr.psnrU = pr.u;
        fr.psnrV = pr.v;

        if (!vmaf.pushFramePair(rc2, dc2, err)) {
            summary.error = err;
            break;
        }
        pushOrder.push_back(static_cast<int>(summary.frames.size()));
        summary.frames.push_back(fr);
        ++pushed;

        // 上一帧的分数通常已就绪 (motion 特征需要下一帧)
        double s;
        if (pushed >= 2 && vmaf.scoreAt(pushed - 2, s)) {
            auto& prev = summary.frames[static_cast<size_t>(pushOrder[static_cast<size_t>(pushed - 2)])];
            prev.score = s;
            prev.scoreValid = true;
            FrameResult notify = prev;
            notify.rgbRef = fr.rgbRef; // 沿用缩略图逻辑见下
            if (cb.onFrame) cb.onFrame(prev);
        }

        // 当前帧缩略图用于预览
        FrameResult preview = fr;
        if (frameToRgbThumb(rf, preview.rgbRef, preview.thumbW, preview.thumbH))
            frameToRgbThumb(df, preview.rgbDist, preview.thumbW, preview.thumbH);
        if (cb.onFrame) cb.onFrame(preview);

        if (cb.onProgress) cb.onProgress(distSampling ? ++progressDone : r + 1, progressTotal);
    }

    refReader->close();
    distReader->close();

    if (stopRequested.load()) {
        summary.error = "已取消";
        if (cb.onFinished) cb.onFinished(summary);
        return;
    }

    if (!summary.error.empty()) {
        if (cb.onFinished) cb.onFinished(summary);
        return;
    }

    if (!vmaf.flush(err)) {
        summary.error = err;
        if (cb.onFinished) cb.onFinished(summary);
        return;
    }

    // 补齐所有帧分数
    for (size_t i = 0; i < pushOrder.size(); ++i) {
        double s;
        if (vmaf.scoreAt(static_cast<int>(i), s)) {
            auto& fr = summary.frames[static_cast<size_t>(pushOrder[i])];
            if (!fr.scoreValid) {
                fr.score = s;
                fr.scoreValid = true;
            }
        }
    }

    if (!vmaf.pooledScore(summary.mean, summary.harmonicMean, summary.minScore,
                          summary.maxScore)) {
        summary.error = "分数池化失败";
        if (cb.onFinished) cb.onFinished(summary);
        return;
    }

    // PSNR 均值 (全部有效样本)
    {
        double sum = 0;
        int n = 0;
        for (const auto& fr : summary.frames) {
            if (!fr.dropped && fr.psnr >= 0) {
                sum += fr.psnr;
                ++n;
            }
        }
        summary.psnrMean = n > 0 ? sum / n : -1.0;
    }

    summary.ok = true;
    log(cb, "info", "评估完成: VMAF mean=" + std::to_string(summary.mean).substr(0, 6) +
                        " harmonic=" + std::to_string(summary.harmonicMean).substr(0, 6));
    {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "帧号统计: 未识别 %d 帧, 参考帧号覆盖率 %.1f%%, 冻结段 %d, 最长冻结 %d 帧, 丢帧 %d",
                 summary.markerUnrecognized, summary.markerCoverage * 100.0,
                 summary.freezeSegments, summary.freezeMaxLen, summary.dropped);
        log(cb, "info", buf);
    }
    if (cb.onFinished) cb.onFinished(summary);
}

} // namespace vt
