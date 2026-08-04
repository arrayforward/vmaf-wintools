#include "frame_aligner.h"

#include <algorithm>
#include <cmath>

namespace vt {

void makeThumb(const Frame& f, Thumb& out) {
    // 将亮度平面降采样到 32x32 (盒式均值)
    const double sx = f.width / 32.0;
    const double sy = f.height / 32.0;
    for (int r = 0; r < 32; ++r) {
        const int y0 = static_cast<int>(r * sy);
        const int y1 = std::max(y0 + 1, static_cast<int>((r + 1) * sy));
        for (int c = 0; c < 32; ++c) {
            const int x0 = static_cast<int>(c * sx);
            const int x1 = std::max(x0 + 1, static_cast<int>((c + 1) * sx));
            // 块内抽样求均值 (块过大时步进抽样, 控制开销)
            const int stepY = std::max(1, (y1 - y0) / 4);
            const int stepX = std::max(1, (x1 - x0) / 4);
            double sum = 0;
            int n = 0;
            for (int py = y0; py < y1; py += stepY)
                for (int px = x0; px < x1; px += stepX) {
                    sum += f.y[static_cast<size_t>(py) * f.strideY + px];
                    ++n;
                }
            out[static_cast<size_t>(r) * 32 + c] = static_cast<float>(sum / n);
        }
    }
}

double ncc(const Thumb& a, const Thumb& b) {
    double sa = 0, sb = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        sa += a[i];
        sb += b[i];
    }
    const double ma = sa / a.size(), mb = sb / b.size();
    double num = 0, da = 0, db = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double xa = a[i] - ma, xb = b[i] - mb;
        num += xa * xb;
        da += xa * xa;
        db += xb * xb;
    }
    const double den = std::sqrt(da * db);
    return den < 1e-9 ? 0.0 : num / den;
}

int FrameAligner::detectGreenTransition(const VideoScan& scan) {
    // 从开头找连续纯色段, 返回段后第一帧
    int run = 0;
    const int minRun = 3; // 至少连续 3 帧纯色才算绿屏段
    for (size_t i = 0; i < scan.solid.size() && i < 60 * 30; ++i) {
        if (scan.solid[i]) {
            ++run;
        } else {
            if (run >= minRun) return static_cast<int>(i);
            run = 0;
        }
    }
    return -1;
}

int FrameAligner::coarseOffset(const VideoScan& ref, const VideoScan& dist,
                               const AlignOptions& opt) {
    if (ref.thumbs.empty() || dist.thumbs.empty()) return 0;

    const int rangeRef = std::min<int>(static_cast<int>(ref.thumbs.size()) - 1,
                                       opt.coarseRangeSec * static_cast<int>(ref.fps + 0.5));
    const int rangeDist = std::min<int>(static_cast<int>(dist.thumbs.size()) - 1,
                                        opt.coarseRangeSec * static_cast<int>(dist.fps + 0.5));

    // 采样参考帧序列作为模板
    const int templateLen = std::min(rangeRef, 60);
    if (templateLen < 5) return 0;

    double bestScore = -2.0;
    int bestOffset = 0;
    for (int dStart = 0; dStart + templateLen <= rangeDist; ++dStart) {
        double sum = 0;
        for (int i = 0; i < templateLen; i += 2)
            sum += ncc(ref.thumbs[static_cast<size_t>(i)], dist.thumbs[static_cast<size_t>(dStart + i)]);
        const double avg = sum / ((templateLen + 1) / 2);
        if (avg > bestScore) {
            bestScore = avg;
            bestOffset = dStart;
        }
    }
    return bestOffset;
}

FrameMapping FrameAligner::buildMapping(const VideoScan& ref, const VideoScan& dist, int refStart,
                                        int distStart, const AlignOptions& opt) {
    FrameMapping m;
    m.refStart = refStart;
    m.distStart = distStart;

    const int nRef = static_cast<int>(ref.thumbs.size());
    const int nDist = static_cast<int>(dist.thumbs.size());
    if (refStart >= nRef || distStart >= nDist) return m;

    m.refToDist.assign(static_cast<size_t>(nRef - refStart), -1);

    size_t d = static_cast<size_t>(distStart);
    int prevMatched = distStart - 1;
    for (size_t r = static_cast<size_t>(refStart); r < ref.thumbs.size(); ++r) {
        // 窗口内找最优目标帧
        double best = -2.0, second = -2.0;
        int bestJ = -1;
        const size_t end = std::min(static_cast<size_t>(nDist), d + static_cast<size_t>(opt.searchWindow));
        for (size_t j = d; j < end; ++j) {
            const double s = ncc(ref.thumbs[r], dist.thumbs[j]);
            if (s > best) {
                second = best;
                best = s;
                bestJ = static_cast<int>(j);
            } else if (s > second) {
                second = s;
            }
        }
        if (bestJ < 0 || best < opt.matchThresh) {
            ++m.droppedCount;
            continue;
        }
        // 消歧: 若下一参考帧与 bestJ 更相似, 说明当前参考帧实为丢帧
        if (r + 1 < ref.thumbs.size() &&
            ncc(ref.thumbs[r + 1], dist.thumbs[static_cast<size_t>(bestJ)]) > best) {
            ++m.droppedCount;
            continue;
        }
        m.refToDist[r - refStart] = bestJ;
        // 跳过的目标帧 (与前一匹配之间) 计入冻结/重复
        if (bestJ > prevMatched + 1) m.freezeCount += bestJ - prevMatched - 1;
        prevMatched = bestJ;
        d = static_cast<size_t>(bestJ) + 1;
    }
    return m;
}

} // namespace vt
