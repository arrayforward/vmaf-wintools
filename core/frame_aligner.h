#pragma once

#include <cstdint>
#include <vector>

namespace vt {

// 一端的完整扫描信息
struct VideoScan {
    std::vector<int> markerIndex; // 每帧的帧号标记, -1 = 未识别
    int markerCount = 0;          // 识别出帧号标记的帧数
    // 所有有效标记的 QR 码区实测外接框均值 (输入图坐标系); markerQrSize<=0 表示无
    double markerQrX = 0, markerQrY = 0, markerQrSize = 0;
    // QR 外接框中心方位推断的画面旋转 (顺时针 90° 的倍数, 0-3): 横屏片源的 QR 在右下,
    // 出现在左下/左上/右上分别对应录屏被旋转 90°CW/180°/270°CW
    int rotation = 0;
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
    int freezeCount = 0;    // 目标帧被重复匹配的次数统计
    int freezeSegments = 0; // 帧号对齐: 目标侧连续重复帧号的段数
    int freezeMaxLen = 0;   // 帧号对齐: 最长冻结段长度 (帧)
};

class FrameAligner {
public:
    // 帧号标记对齐: 按 QR 帧号精确匹配 (refStart/distStart 恒为 0)
    // 目标侧同一帧号多次出现取首次, 重复次数计入 freezeCount;
    // 参考侧有帧号而目标侧缺失计入 droppedCount; 未识别的帧不映射 (refToDist = -1)
    static FrameMapping buildMarkerMapping(const VideoScan& ref, const VideoScan& dist);
};

} // namespace vt
