// jank_cli: 录屏卡顿率分析 (PerfDog 标准)
//
// 用法:
//   jank_cli <录屏视频> [--export-csv <path>]
// 录屏内容为带 QR 帧号标记的测试片源 (marker_burn 烧录) 在设备上的播放画面。
// 退出码: 分析成功 0, 失败 1

#include "core/jank.h"
#include "core/marker.h"
#include "core/video_reader.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <录屏视频> [--export-csv <path>]\n", argv[0]);
        return 2;
    }
    const std::string input = argv[1];
    std::string csvPath;
    for (int i = 2; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--export-csv") == 0) csvPath = argv[i + 1];
    }

    std::string err;
    auto reader = vt::createReader(input, "", 0.0, err);
    if (!reader || !reader->open(err)) {
        fprintf(stderr, "[错误] 无法打开 %s: %s\n", input.c_str(), err.c_str());
        return 1;
    }
    const double fps = reader->fps();
    printf("[信息] %s: %dx%d @ %.3f fps\n", input.c_str(), reader->width(), reader->height(), fps);

    std::vector<int> indices;
    vt::Frame f;
    while (reader->readFrame(f)) {
        vt::MarkerInfo mi;
        if (vt::decodeMarker(f, mi) && mi.valid)
            indices.push_back(mi.frameIndex);
        else
            indices.push_back(-1);
        if (indices.size() % 300 == 0) printf("[进度] 已解码 %zu 帧...\n", indices.size());
    }
    reader->close();
    printf("[信息] 共 %zu 采集帧\n", indices.size());
    if (indices.empty()) {
        fprintf(stderr, "[错误] 视频无帧\n");
        return 1;
    }

    const vt::JankReport rep = vt::analyzeJank(indices, fps);

    printf("==== 卡顿分析报告 ====\n");
    printf("录屏帧率: %.3f fps, 时长: %.1f s\n", rep.captureFps, rep.durationMs / 1000.0);
    printf("distinct 源帧数: %d, 未识别采集帧: %d\n", rep.totalFrames, rep.unrecognized);
    printf("平均帧耗时: %.2f ms, 最大帧耗时: %.2f ms\n", rep.avgFrameMs, rep.maxFrameMs);
    printf("Jank: %d 次, Big Jank: %d 次\n", rep.jankCount, rep.bigJankCount);
    printf("卡顿率: %.2f%%, 严重卡顿率: %.2f%%, Jank/10min: %.1f\n", rep.jankRate * 100.0,
           rep.bigJankRate * 100.0, rep.jankPer10Min);
    for (const auto& e : rep.events) {
        printf("  [%6lld ms] 源帧 %d 显示 %.1f ms %s\n", static_cast<long long>(e.timeMs),
               e.frameIndex, e.displayMs, e.big ? "BigJank" : "Jank");
    }

    if (!csvPath.empty()) {
        FILE* fp = fopen(csvPath.c_str(), "w");
        if (!fp) {
            fprintf(stderr, "[错误] 无法写 CSV: %s\n", csvPath.c_str());
            return 1;
        }
        fprintf(fp, "seq,frameIndex,displayMs,jank,bigJank\n");
        // 逐段导出: 段聚合与卡顿判定都来自 analyzeJank (rep.segments)
        for (size_t i = 0; i < rep.segments.size(); ++i) {
            const auto& s = rep.segments[i];
            fprintf(fp, "%zu,%d,%.2f,%d,%d\n", i, s.frameIndex, s.displayMs, s.jank ? 1 : 0,
                    s.bigJank ? 1 : 0);
        }
        fclose(fp);
        printf("[信息] CSV 已导出: %s (%zu 行)\n", csvPath.c_str(), rep.segments.size());
    }
    return 0;
}
