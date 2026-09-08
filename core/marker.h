#pragma once

#include <cstdint>

#include "frame.h"

namespace vt {

// 帧号 QR 标记规范:
//   载荷 = 帧号 8 位十进制字符串 (左补零, 如 "00000123")
//   纠错等级 H, 最小版本 (8 位数字 -> version 1, 21x21 模块)
//   位置: 帧右下角, 静区 4 模块, 静区外缘距帧右/底边缘 2 模块
//   模块尺寸 m = max(8, round(W/160)), 向下取偶数
//   黑模块 Y=16, 白模块与静区 Y=235, 标记覆盖矩形 U=V=128

struct MarkerInfo {
    bool valid = false;
    int frameIndex = -1;
    // QR 码区 (不含静区) 在输入图像中的实测外接框 (quirc 四角, 假设无旋转)
    // 输入图可能是缩放/信箱化的采集帧, 源帧分辨率需由调用方提供
    int qrX = 0, qrY = 0, qrSize = 0;
};

// 烧录帧号 QR 标记到帧 (原地修改 Y/U/V 平面)
bool burnMarker(Frame& f, int frameIndex);

// 从 YUV 帧解码 (用 Y 平面)
bool decodeMarker(const Frame& f, MarkerInfo& out);

// 从 RGB24 图解码 (adb 截图经 QImage/stb 解码后的 RGB)
bool decodeMarkerRgb(const uint8_t* rgb, int w, int h, int stride, MarkerInfo& out);

// 由 QR 实测外接框 + 源视频分辨率反推输入图中的视频区域 (支持缩放/信箱黑边)
// 源帧中码区名义矩形: 边长 21*m, 左上角 (srcW-27m, srcH-27m); k = qrSize/(21*m)
// qrX/qrY/qrSize 支持传入多次测量的均值 (double), 降低单帧像素量化误差
// 失败 (参数无效) 返回 false
bool videoRectFromMarker(double qrX, double qrY, double qrSize, int srcW, int srcH, int& outX,
                         int& outY, int& outW, int& outH);

} // namespace vt
