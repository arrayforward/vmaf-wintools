// eval_cli: 命令行 VMAF 画质评测 (帧号对齐 + 自动裁剪)
//
// 用法:
//   eval_cli <ref视频> <dist视频或截图文件夹> [model路径]
//   model 默认 <exe目录>/models/vmaf_v0.6.1.json
// 退出码: 评估成功 0, 失败 1

#include "core/pipeline.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::string exeDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf, n);
    const size_t pos = p.find_last_of("\\/");
    return pos == std::string::npos ? "." : p.substr(0, pos);
#else
    return ".";
#endif
}

bool fileExists(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "用法: %s <ref视频> <dist视频或截图文件夹> [model路径]\n", argv[0]);
        return 2;
    }

    vt::PipelineConfig cfg;
    cfg.refPath = argv[1];
    cfg.distPath = argv[2];
    if (argc > 3) {
        cfg.vmaf.modelName = argv[3];
    } else {
        const std::string def = exeDir() + "/models/vmaf_v0.6.1.json";
        if (fileExists(def)) cfg.vmaf.modelName = def;
    }
    // 对齐与裁剪全自动: 帧号精确对齐 + QR 反推各端视频区域

    vt::Summary summary;
    vt::PipelineCallbacks cb;
    cb.onLog = [](const std::string& level, const std::string& msg) {
        printf("[%s] %s\n", level.c_str(), msg.c_str());
    };
    cb.onFinished = [&](const vt::Summary& s) { summary = s; };

    vt::Pipeline pipe;
    pipe.run(cfg, cb);

    printf("====\n");
    if (!summary.error.empty())
        printf("错误: %s\n", summary.error.c_str());
    printf("VMAF mean=%.4f harmonic=%.4f min=%.4f max=%.4f\n", summary.mean, summary.harmonicMean,
           summary.minScore, summary.maxScore);
    printf("匹配 %d 对, 丢帧 %d, 冻结重复 %d, 冻结段 %d, 最长冻结 %d 帧\n", summary.matched,
           summary.dropped, summary.frozen, summary.freezeSegments, summary.freezeMaxLen);
    printf("帧号: 未识别 %d 帧, 参考覆盖率 %.1f%%\n", summary.markerUnrecognized,
           summary.markerCoverage * 100.0);
    return summary.ok ? 0 : 1;
}
