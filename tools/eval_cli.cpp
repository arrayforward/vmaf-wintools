// eval_cli: 命令行 VMAF 画质评测 (帧号对齐 + 自动裁剪)
//
// 用法:
//   eval_cli <ref视频> <dist视频或截图文件夹> [model路径] [--export-json 文件] [--export-csv 文件]
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

// JSON 字符串转义 (路径含反斜杠)
std::string jsonEscape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

// 导出逐帧结果 (Summary.frames 全量, 含 flush 后补齐的分数)
// JSON: {reference, distorted, summary{...}, frames[{ref_frame, dist_frame, vmaf, status}]}
bool exportJson(const std::string& path, const vt::PipelineConfig& cfg, const vt::Summary& s) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "{\n");
    fprintf(f, "  \"reference\": \"%s\",\n  \"distorted\": \"%s\",\n",
            jsonEscape(cfg.refPath).c_str(), jsonEscape(cfg.distPath).c_str());
    fprintf(f,
            "  \"summary\": {\"mean\": %.4f, \"harmonic_mean\": %.4f, \"min\": %.4f, "
            "\"max\": %.4f, \"psnr_mean\": %.4f, \"matched\": %d, \"dropped\": %d, \"frozen\": %d, "
            "\"freeze_segments\": %d, \"freeze_max_len\": %d, \"marker_unrecognized\": %d, "
            "\"marker_coverage\": %.4f},\n",
            s.mean, s.harmonicMean, s.minScore, s.maxScore, s.psnrMean, s.matched, s.dropped,
            s.frozen, s.freezeSegments, s.freezeMaxLen, s.markerUnrecognized, s.markerCoverage);
    fprintf(f, "  \"frames\": [\n");
    for (size_t i = 0; i < s.frames.size(); ++i) {
        const auto& fr = s.frames[i];
        fprintf(f, "    {\"ref_frame\": %d, ", fr.refIdx);
        if (fr.dropped) {
            fprintf(f, "\"status\": \"dropped\"}");
        } else {
            fprintf(f, "\"dist_frame\": %d, ", fr.distIdx);
            if (fr.scoreValid) fprintf(f, "\"vmaf\": %.4f, ", fr.score);
            if (fr.psnr >= 0) fprintf(f, "\"psnr\": %.4f, ", fr.psnr);
            fprintf(f, "\"status\": \"matched\"}");
        }
        fprintf(f, "%s\n", i + 1 < s.frames.size() ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return true;
}

// CSV 列: ref_frame,dist_frame,vmaf,psnr,status
bool exportCsv(const std::string& path, const vt::Summary& s) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "ref_frame,dist_frame,vmaf,psnr,status\n");
    for (const auto& fr : s.frames) {
        fprintf(f, "%d,", fr.refIdx);
        if (!fr.dropped) fprintf(f, "%d", fr.distIdx);
        fprintf(f, ",");
        if (!fr.dropped && fr.scoreValid) fprintf(f, "%.4f", fr.score);
        fprintf(f, ",");
        if (!fr.dropped && fr.psnr >= 0) fprintf(f, "%.4f", fr.psnr);
        fprintf(f, ",%s\n", fr.dropped ? "dropped" : "matched");
    }
    fclose(f);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr,
                "用法: %s <ref视频> <dist视频或截图文件夹> [model路径] "
                "[--export-json 文件] [--export-csv 文件]\n",
                argv[0]);
        return 2;
    }

    vt::PipelineConfig cfg;
    cfg.refPath = argv[1];
    cfg.distPath = argv[2];
    std::string exportJsonPath, exportCsvPath;
    bool modelGiven = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--export-json" && i + 1 < argc) {
            exportJsonPath = argv[++i];
        } else if (a == "--export-csv" && i + 1 < argc) {
            exportCsvPath = argv[++i];
        } else {
            cfg.vmaf.modelName = a; // 位置参数: model 路径
            modelGiven = true;
        }
    }
    if (!modelGiven) {
        const std::string def = exeDir() + "/models/vmaf_v0.6.1.json";
        if (fileExists(def)) cfg.vmaf.modelName = def;
    }
    // 对齐与裁剪全自动: 帧号精确对齐 + QR 反推各端视频区域

    vt::Summary summary;
    vt::PipelineCallbacks cb;
    cb.onLog = [](const std::string& level, const std::string& msg) {
        printf("[%s] %s\n", level.c_str(), msg.c_str());
    };
    cb.onFrame = [](const vt::FrameResult& fr) {
        if (fr.scoreValid) printf("帧 %d VMAF=%.4f\n", fr.refIdx, fr.score);
    };
    cb.onFinished = [&](const vt::Summary& s) { summary = s; };

    vt::Pipeline pipe;
    pipe.run(cfg, cb);

    printf("====\n");
    if (!summary.error.empty())
        printf("错误: %s\n", summary.error.c_str());
    printf("VMAF mean=%.4f harmonic=%.4f min=%.4f max=%.4f\n", summary.mean, summary.harmonicMean,
           summary.minScore, summary.maxScore);
    printf("PSNR mean=%.2f dB\n", summary.psnrMean);
    printf("匹配 %d 对, 丢帧 %d, 冻结重复 %d, 冻结段 %d, 最长冻结 %d 帧\n", summary.matched,
           summary.dropped, summary.frozen, summary.freezeSegments, summary.freezeMaxLen);
    printf("帧号: 未识别 %d 帧, 参考覆盖率 %.1f%%\n", summary.markerUnrecognized,
           summary.markerCoverage * 100.0);

    if (summary.ok && !exportJsonPath.empty()) {
        if (exportJson(exportJsonPath, cfg, summary))
            printf("已导出 JSON: %s (%zu 帧)\n", exportJsonPath.c_str(), summary.frames.size());
        else
            printf("错误: 无法写 %s\n", exportJsonPath.c_str());
    }
    if (summary.ok && !exportCsvPath.empty()) {
        if (exportCsv(exportCsvPath, summary))
            printf("已导出 CSV: %s (%zu 帧)\n", exportCsvPath.c_str(), summary.frames.size());
        else
            printf("错误: 无法写 %s\n", exportCsvPath.c_str());
    }
    return summary.ok ? 0 : 1;
}
