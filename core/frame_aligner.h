#pragma once

#include "frame.h"

#include <array>
#include <vector>

namespace vt {

// 32x32 灰度缩略图, 用于内容匹配
using Thumb = std::array<float, 1024>;

void makeThumb(const Frame& f, Thumb& out);
double ncc(const Thumb& a, const Thumb& b); // [-1, 1]

struct AlignOptions {
    int searchWindow = 16;      // 单调匹配时的前向搜索窗口 (目标帧数)
    double matchThresh = 0.6;   // NCC 阈值, 低于此值判定为丢帧
    int coarseRangeSec = 10;    // 粗对齐搜索范围 (秒)
};

// 一端的完整扫描信息
struct VideoScan {
    std::vector<Thumb> thumbs;
    std::vector<char> solid; // 是否纯色帧 (绿屏)
    int64_t totalFrames = 0;
    double fps = 0;
    int width = 0, height = 0;
};

struct FrameMapping {
    // 对每个参考帧: 匹配的目标帧号, -1 = 丢帧
    std::vector<int> refToDist;
    int refStart = 0;  // 对齐后参考起始帧
    int distStart = 0; // 对齐后目标起始帧
    int droppedCount = 0;
    int freezeCount = 0; // 目标帧被重复匹配的次数统计
};

class FrameAligner {
public:
    // 绿屏标定: 找开头连续纯色帧段的结束位置, 返回首个内容帧号; 失败返回 -1
    static int detectGreenTransition(const VideoScan& scan);

    // 粗对齐: 在 ±coarseRange 内搜索 NCC 序列最优偏移, 返回目标相对参考的帧偏移
    // (distIdx ≈ refIdx + offset)
    static int coarseOffset(const VideoScan& ref, const VideoScan& dist, const AlignOptions& opt);

    // 单调序列匹配, 生成帧映射
    static FrameMapping buildMapping(const VideoScan& ref, const VideoScan& dist, int refStart,
                                     int distStart, const AlignOptions& opt);
};

} // namespace vt
