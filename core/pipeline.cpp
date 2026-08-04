#include "pipeline.h"

#include "video_reader.h"

#include <algorithm>
#include <cmath>

namespace vt {

namespace {

void log(const PipelineCallbacks& cb, const std::string& level, const std::string& msg) {
    if (cb.onLog) cb.onLog(level, msg);
}

// 第一遍扫描: 提取全部帧的缩略图和纯色标记
bool scanVideo(VideoReader& reader, VideoScan& scan, std::atomic<bool>& stop,
               const PipelineCallbacks& cb, const char* tag) {
    Frame f;
    int idx = 0;
    while (!stop.load() && reader.readFrame(f)) {
        Thumb t;
        makeThumb(f, t);
        scan.thumbs.push_back(t);
        scan.solid.push_back(isSolidColorFrame(f) ? 1 : 0);
        if (++idx % 300 == 0)
            log(cb, "info", std::string(tag) + " 扫描 " + std::to_string(idx) + " 帧...");
    }
    scan.totalFrames = static_cast<int64_t>(scan.thumbs.size());
    scan.fps = reader.fps();
    scan.width = reader.width();
    scan.height = reader.height();
    return !stop.load();
}

CropRect resolveCrop(const CropRect& c, int w, int h) {
    if (!c.autoCenter()) return c;
    CropRect r;
    r.w = (w / 2) & ~1;
    r.h = (h / 2) & ~1;
    r.x = ((w - r.w) / 2) & ~1;
    r.y = ((h - r.h) / 2) & ~1;
    return r;
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

} // namespace

void Pipeline::run(const PipelineConfig& cfg, const PipelineCallbacks& cb) {
    Summary summary;

    // ---------- 第一遍: 扫描两端视频 ----------
    VideoScan scanRef, scanDist;
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
        if (!scanVideo(*reader, scanDist, stopRequested, cb, "[目标]")) {
            summary.error = "已取消";
            if (cb.onFinished) cb.onFinished(summary);
            return;
        }
        reader->close();
    }

    log(cb, "info", "扫描完成: 参考 " + std::to_string(scanRef.totalFrames) + " 帧, 目标 " +
                        std::to_string(scanDist.totalFrames) + " 帧");

    // ---------- 对齐 ----------
    int refStart = 0, distStart = 0;
    bool hasGreenAnchor = false;
    {
        // 两种模式都先尝试绿屏锚点: 内容模式遇到绿屏视频时,
        // 直接做 NCC 粗对齐会因绿屏帧完全相同而任意错位
        const int rAnchor = FrameAligner::detectGreenTransition(scanRef);
        const int dAnchor = FrameAligner::detectGreenTransition(scanDist);
        hasGreenAnchor = (rAnchor >= 0 && dAnchor >= 0);
        if (hasGreenAnchor) {
            refStart = rAnchor;
            distStart = dAnchor;
            log(cb, "info", "绿屏锚点: 参考帧 " + std::to_string(rAnchor) + ", 目标帧 " +
                                std::to_string(dAnchor) +
                                (cfg.alignMode == AlignMode::Content ? " (内容模式自动启用)" : ""));
        }
    }
    if (cfg.alignMode == AlignMode::GreenScreen && !hasGreenAnchor) {
        log(cb, "warn", "未检测到绿屏段, 退化为内容自动匹配");
        const int off = FrameAligner::coarseOffset(scanRef, scanDist, cfg.alignOpt);
        distStart = std::max(0, off);
    } else if (cfg.alignMode == AlignMode::Content && !hasGreenAnchor) {
        const int off = FrameAligner::coarseOffset(scanRef, scanDist, cfg.alignOpt);
        distStart = std::max(0, off);
        log(cb, "info", "内容粗对齐偏移: " + std::to_string(distStart) + " 帧");
    } else if (cfg.alignMode == AlignMode::None && !hasGreenAnchor) {
        // 不对齐
    }

    FrameMapping mapping =
        FrameAligner::buildMapping(scanRef, scanDist, refStart, distStart, cfg.alignOpt);

    int matched = 0;
    for (int d : mapping.refToDist)
        if (d >= 0) ++matched;
    summary.dropped = mapping.droppedCount;
    summary.frozen = mapping.freezeCount;
    summary.matched = matched;
    log(cb, "info", "帧映射: 匹配 " + std::to_string(matched) + " 对, 丢帧 " +
                        std::to_string(mapping.droppedCount) + ", 冻结/重复目标帧 " +
                        std::to_string(mapping.freezeCount));

    if (matched == 0) {
        summary.error = "没有匹配到任何帧对, 无法评估 (两端内容差异过大, 或对齐模式不合适)";
        if (cb.onFinished) cb.onFinished(summary);
        return;
    }

    // ---------- 裁剪参数 (规范化: 偶数对齐 + 边界裁剪) ----------
    const CropRect rc = normalizeCrop(resolveCrop(cfg.refCrop, scanRef.width, scanRef.height),
                                      scanRef.width, scanRef.height);
    const CropRect dc = normalizeCrop(resolveCrop(cfg.distCrop, scanDist.width, scanDist.height),
                                      scanDist.width, scanDist.height);
    if (rc.w <= 0 || rc.h <= 0 || dc.w <= 0 || dc.h <= 0) {
        summary.error = "裁剪区域无效 (超出画面范围), 请重新框选";
        if (cb.onFinished) cb.onFinished(summary);
        return;
    }
    log(cb, "info", "裁剪: 参考 " + std::to_string(rc.w) + "x" + std::to_string(rc.h) + " @(" +
                        std::to_string(rc.x) + "," + std::to_string(rc.y) + "), 目标 " +
                        std::to_string(dc.w) + "x" + std::to_string(dc.h) + " @(" +
                        std::to_string(dc.x) + "," + std::to_string(dc.y) + ")");

    // ---------- 初始化 VMAF ----------
    VmafEvaluator vmaf;
    {
        std::string err;
        if (!vmaf.init(cfg.vmaf, rc.w, rc.h, err)) {
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
    int distWanted = mapping.distStart;
    Frame rf, df;
    int pushed = 0;
    std::vector<int> pushOrder; // pushOrder[i] = refToDist 下标

    // 跳过对齐起点之前的帧
    for (int i = 0; i < refStart; ++i)
        if (!refReader->readFrame(rf)) break;
    for (int i = 0; i < mapping.distStart; ++i)
        if (!distReader->readFrame(df)) break;

    for (int r = 0; r < totalRef && !stopRequested.load(); ++r) {
        const int dIdx = mapping.refToDist[static_cast<size_t>(r)];
        FrameResult fr;
        fr.refIdx = r + refStart;
        fr.distIdx = dIdx;

        // 每个参考帧都必须读取, 保持解码顺序
        if (!refReader->readFrame(rf)) break;

        if (dIdx < 0) {
            fr.dropped = true;
            summary.frames.push_back(fr);
            if (cb.onFrame) cb.onFrame(fr);
            if (cb.onProgress) cb.onProgress(r + 1, totalRef);
            log(cb, "warn", "帧 " + std::to_string(fr.refIdx) + " 丢帧");
            continue;
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
            fr.dropped = true;
            summary.frames.push_back(fr);
            if (cb.onFrame) cb.onFrame(fr);
            continue;
        }

        Frame rc2 = cropFrame(rf, rc.x, rc.y, rc.w, rc.h);
        Frame dc2 = cropFrame(df, dc.x, dc.y, dc.w, dc.h);
        if (dc2.width != rc2.width || dc2.height != rc2.height)
            dc2 = scaleFrame(dc2, rc2.width, rc2.height);

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

        if (cb.onProgress) cb.onProgress(r + 1, totalRef);
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

    summary.ok = true;
    log(cb, "info", "评估完成: VMAF mean=" + std::to_string(summary.mean).substr(0, 6) +
                        " harmonic=" + std::to_string(summary.harmonicMean).substr(0, 6));
    if (cb.onFinished) cb.onFinished(summary);
}

} // namespace vt
