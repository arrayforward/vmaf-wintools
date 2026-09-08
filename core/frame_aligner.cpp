#include "frame_aligner.h"

#include <algorithm>
#include <unordered_map>

namespace vt {

FrameMapping FrameAligner::buildMarkerMapping(const VideoScan& ref, const VideoScan& dist) {
    FrameMapping m;
    m.refStart = 0;
    m.distStart = 0;

    const int nRef = static_cast<int>(ref.markerIndex.size());
    const int nDist = static_cast<int>(dist.markerIndex.size());
    m.refToDist.assign(static_cast<size_t>(nRef), -1);

    // 目标侧: 帧号 -> 首次出现的帧位置; 重复出现 (冻结) 计入 freezeCount
    std::unordered_map<int, int> firstOcc;
    firstOcc.reserve(static_cast<size_t>(nDist) * 2);
    for (int d = 0; d < nDist; ++d) {
        const int idx = dist.markerIndex[static_cast<size_t>(d)];
        if (idx < 0) continue;
        if (!firstOcc.emplace(idx, d).second) ++m.freezeCount;
    }

    // 冻结段统计: 目标侧连续相同帧号的最长段
    for (int d = 0; d < nDist;) {
        int e = d + 1;
        while (e < nDist && dist.markerIndex[static_cast<size_t>(d)] >= 0 &&
               dist.markerIndex[static_cast<size_t>(e)] == dist.markerIndex[static_cast<size_t>(d)])
            ++e;
        const int len = e - d;
        if (dist.markerIndex[static_cast<size_t>(d)] >= 0 && len >= 2) {
            ++m.freezeSegments;
            m.freezeMaxLen = std::max(m.freezeMaxLen, len);
        }
        d = e;
    }

    // 参考侧逐帧按帧号匹配
    for (int r = 0; r < nRef; ++r) {
        const int idx = ref.markerIndex[static_cast<size_t>(r)];
        if (idx < 0) continue; // 未识别: 跳过
        const auto it = firstOcc.find(idx);
        if (it == firstOcc.end()) {
            ++m.droppedCount;
            continue;
        }
        m.refToDist[static_cast<size_t>(r)] = it->second;
    }
    return m;
}

} // namespace vt
