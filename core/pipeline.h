#pragma once

#include "frame_aligner.h"
#include "vmaf_evaluator.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace vt {

struct PipelineConfig {
    std::string refPath;
    std::string distPath; // 视频文件 / image2 序列模式 (shot_%05d.png) / 截图文件夹
    std::string yuvRefSize = "1920x1080"; // 仅 .yuv 输入时需要
    std::string yuvDistSize = "1920x1080";
    double yuvFps = 30.0;

    VmafConfig vmaf;
    // 对齐与裁剪全自动: 两端帧号识别率 >= 80% 时按 QR 帧号精确对齐,
    // 并由 QR 外接框反推各端视频区域; 识别率不足直接报错
};

struct FrameResult {
    int refIdx = -1;
    int distIdx = -1;
    bool dropped = false;
    bool scoreValid = false;
    double score = 0;
    // RGB24 缩略图 (可能为空)
    std::vector<uint8_t> rgbRef, rgbDist;
    int thumbW = 0, thumbH = 0;
};

struct Summary {
    int matched = 0;
    int dropped = 0;
    int frozen = 0;
    // 帧号标记统计 (两端均未烧录帧号时为 0)
    int markerUnrecognized = 0; // 两端未识别帧号的帧数合计
    double markerCoverage = 0;  // 参考帧帧号覆盖率 (识别出帧号的比例)
    int freezeSegments = 0;     // 目标侧连续重复帧号的段数
    int freezeMaxLen = 0;       // 最长冻结段长度 (帧)
    double mean = 0, harmonicMean = 0, minScore = 0, maxScore = 0;
    std::vector<FrameResult> frames; // 不含 rgb (内存考虑)
    bool ok = false;
    std::string error;
};

struct PipelineCallbacks {
    std::function<void(const std::string& level, const std::string& msg)> onLog;
    std::function<void(int done, int total)> onProgress;
    std::function<void(const FrameResult&)> onFrame;
    std::function<void(const Summary&)> onFinished;
};

class Pipeline {
public:
    std::atomic<bool> stopRequested{false};

    // 同步执行 (调用方负责放到工作线程)
    void run(const PipelineConfig& cfg, const PipelineCallbacks& cb);
};

} // namespace vt
