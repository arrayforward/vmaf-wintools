#pragma once

#include "frame_aligner.h"
#include "vmaf_evaluator.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace vt {

enum class AlignMode { None, GreenScreen, Content };

struct CropRect {
    int x = 0, y = 0, w = 0, h = 0; // w/h = 0 表示 "自动中心 1/2"
    bool autoCenter() const { return w <= 0 || h <= 0; }
};

struct PipelineConfig {
    std::string refPath;
    std::string distPath;
    std::string yuvRefSize = "1920x1080"; // 仅 .yuv 输入时需要
    std::string yuvDistSize = "1920x1080";
    double yuvFps = 30.0;

    VmafConfig vmaf;
    AlignMode alignMode = AlignMode::Content;
    AlignOptions alignOpt;

    CropRect refCrop;  // 参考视频裁剪 (各自坐标系)
    CropRect distCrop; // 目标视频裁剪
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
