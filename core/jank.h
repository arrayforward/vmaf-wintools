#pragma once

#include <cstdint>
#include <vector>

namespace vt {

struct JankEvent {
    int64_t timeMs = 0;   // 卡顿段在录屏时间线上的起始时刻
    double displayMs = 0; // 该源帧的显示时长
    bool big = false;     // 是否严重卡顿 (Big Jank)
    int frameIndex = -1;  // 源帧号
};

// 逐显示段聚合结果 (连续相同源帧号合为一段), 供事件列表/CSV 导出共用
struct JankSegment {
    int frameIndex = -1;    // 源帧号
    double displayMs = 0;   // 显示时长
    int64_t startMs = 0;    // 段首采集帧在录屏时间线上的时刻
    bool jank = false, bigJank = false;
};

struct JankReport {
    int totalFrames = 0;   // distinct 源帧数
    int unrecognized = 0;  // QR 未识别的采集帧数
    double captureFps = 0;
    double durationMs = 0;
    double avgFrameMs = 0, maxFrameMs = 0;
    int jankCount = 0, bigJankCount = 0;
    double jankRate = 0;    // jank 帧数 / 总帧数
    double bigJankRate = 0;
    double jankPer10Min = 0; // Jank/10分钟
    std::vector<JankEvent> events;
    std::vector<JankSegment> segments;
};

// PerfDog 卡顿判定: 对每个 distinct 源帧 (按显示顺序), 其显示时长 d 满足
//   d > 前 3 个 distinct 帧平均耗时 * 2 且 d > 84ms  -> Jank
//   d > 前 3 个 distinct 帧平均耗时 * 2 且 d > 125ms -> Big Jank (同时计入 Jank)
// 前 3 帧不足时不判定。
// frameIndices: 逐采集帧的源帧号序列 (-1 = 未识别, 跳过不参与时长累计);
// 片源循环播放导致帧号回绕属正常, 不构成卡顿。captureFps 为录屏帧率。
JankReport analyzeJank(const std::vector<int>& frameIndices, double captureFps);

} // namespace vt
