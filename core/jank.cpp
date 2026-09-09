#include "jank.h"

#include <algorithm>

namespace vt {

JankReport analyzeJank(const std::vector<int>& frameIndices, double captureFps) {
    JankReport rep;
    rep.captureFps = captureFps;
    if (captureFps <= 0.01) return rep;
    const double capFrameMs = 1000.0 / captureFps;
    rep.durationMs = frameIndices.size() * capFrameMs;

    // 聚合连续相同帧号的采集帧为一次显示段; 未识别帧跳过, 不打断段也不计时长
    struct Seg {
        int frameIndex;
        int count;
        int64_t startMs; // 段首采集帧在录屏时间线上的时刻
    };
    std::vector<Seg> segs;
    for (size_t i = 0; i < frameIndices.size(); ++i) {
        const int idx = frameIndices[i];
        if (idx < 0) {
            ++rep.unrecognized;
            continue;
        }
        if (!segs.empty() && segs.back().frameIndex == idx) {
            ++segs.back().count;
        } else {
            segs.push_back({idx, 1, static_cast<int64_t>(i * capFrameMs + 0.5)});
        }
    }

    rep.totalFrames = static_cast<int>(segs.size());
    rep.segments.reserve(segs.size());
    double sumMs = 0;
    for (size_t i = 0; i < segs.size(); ++i) {
        const double d = segs[i].count * capFrameMs;
        sumMs += d;
        rep.maxFrameMs = std::max(rep.maxFrameMs, d);
        JankSegment seg{segs[i].frameIndex, d, segs[i].startMs, false, false};
        if (i >= 3) { // 前 3 帧不足时不判定
            const double prevAvg =
                (segs[i - 1].count + segs[i - 2].count + segs[i - 3].count) * capFrameMs / 3.0;
            if (d > prevAvg * 2.0 && d > 84.0) {
                seg.jank = true;
                seg.bigJank = d > 125.0;
                rep.events.push_back({segs[i].startMs, d, seg.bigJank, segs[i].frameIndex});
                ++rep.jankCount;
                if (seg.bigJank) ++rep.bigJankCount;
            }
        }
        rep.segments.push_back(seg);
    }
    rep.avgFrameMs = rep.totalFrames > 0 ? sumMs / rep.totalFrames : 0;
    rep.jankRate = rep.totalFrames > 0 ? static_cast<double>(rep.jankCount) / rep.totalFrames : 0;
    rep.bigJankRate =
        rep.totalFrames > 0 ? static_cast<double>(rep.bigJankCount) / rep.totalFrames : 0;
    rep.jankPer10Min = rep.durationMs > 0 ? rep.jankCount / (rep.durationMs / 600000.0) : 0;
    return rep;
}

} // namespace vt
