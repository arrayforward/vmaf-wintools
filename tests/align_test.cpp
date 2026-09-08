// align_test: 帧号对齐 + 截图序列输入的端到端验证 (console)
//
// 用法: align_test [repoRoot]   (默认当前目录, 需先构建 marker_burn)
//
// 步骤:
//   1. marker_burn 给 testdata/ref.mp4 烧码 -> testdata/tmp/ref_marked.mp4
//   2. 合成目标视频: 丢 10% 帧 + 冻结重复 5% 帧 + 转 24fps -> testdata/tmp/dist_sim.mp4
//   3. 跑 pipeline 验证帧号精确对齐路径, 校验映射与统计
//   4. 导出 dist 的 PNG 截图序列, 以文件夹作为 distPath 再跑一遍, 校验 image2 输入

#include "core/frame.h"
#include "core/marker.h"
#include "core/pipeline.h"
#include "core/video_reader.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::string g_root = ".";

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

std::string findFfmpeg() {
    const std::string dir = exeDir();
    const std::string local = dir + "/ffmpeg.exe";
    if (fileExists(local)) return local;
    const std::string repo =
        dir + "/../../third_party/ffmpeg/ffmpeg-master-latest-win64-gpl-shared/bin/ffmpeg.exe";
    if (fileExists(repo)) return repo;
    return "ffmpeg";
}

int g_fail = 0;
#define CHECK(cond, ...)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            printf("  [FAIL] " __VA_ARGS__);                         \
            printf("\n");                                            \
            ++g_fail;                                                \
        }                                                            \
    } while (0)

// 扫描视频每帧的帧号标记 (-1 = 未识别)
std::vector<int> scanMarkers(const std::string& path) {
    std::string err;
    auto r = vt::createReader(path, "", 0.0, err);
    std::vector<int> out;
    if (!r || !r->open(err)) {
        printf("[错误] 无法打开 %s: %s\n", path.c_str(), err.c_str());
        ++g_fail;
        return out;
    }
    vt::Frame f;
    while (r->readFrame(f)) {
        vt::MarkerInfo mi;
        if (vt::decodeMarker(f, mi) && mi.valid)
            out.push_back(mi.frameIndex);
        else
            out.push_back(-1);
    }
    r->close();
    return out;
}

bool writePlane(FILE* pipe, const uint8_t* data, int stride, int w, int h) {
    for (int r = 0; r < h; ++r) {
        if (fwrite(data + static_cast<size_t>(r) * stride, 1, w, pipe) != static_cast<size_t>(w))
            return false;
    }
    return true;
}

// 合成目标视频: idx%10==3 丢帧 (10%), idx%20==7 冻结重复 3 次 (5% 的帧), 输出 24fps
// distTruth[d] = 目标第 d 帧对应的参考帧号
bool synthesizeDist(const std::string& refPath, const std::string& distPath,
                    std::vector<int>& distTruth) {
    std::string err;
    auto reader = vt::createReader(refPath, "", 0.0, err);
    if (!reader || !reader->open(err)) {
        printf("[错误] 无法打开 %s: %s\n", refPath.c_str(), err.c_str());
        return false;
    }
    const int w = reader->width();
    const int h = reader->height();

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "\"\"%s\" -y -f rawvideo -pix_fmt yuv420p -s %dx%d -r 24 -i - "
             "-c:v libx264 -crf 16 -preset fast -pix_fmt yuv420p -an \"%s\"\"",
             findFfmpeg().c_str(), w, h, distPath.c_str());
    printf("[命令] %s\n", cmd);
    FILE* pipe = _popen(cmd, "wb");
    if (!pipe) {
        printf("[错误] 无法启动 ffmpeg 子进程\n");
        return false;
    }

    vt::Frame f;
    int idx = 0;
    int dropped = 0, frozen = 0;
    bool ok = true;
    while (reader->readFrame(f)) {
        if (idx % 10 == 3) { // 丢帧
            ++dropped;
            ++idx;
            continue;
        }
        const int copies = (idx % 20 == 7) ? 3 : 1; // 冻结: 连续重复
        for (int c = 0; c < copies; ++c) {
            if (!writePlane(pipe, f.y.data(), f.strideY, w, h) ||
                !writePlane(pipe, f.u.data(), f.strideU, (w + 1) / 2, (h + 1) / 2) ||
                !writePlane(pipe, f.v.data(), f.strideV, (w + 1) / 2, (h + 1) / 2)) {
                printf("[错误] 第 %d 帧写管道失败\n", idx);
                ok = false;
                break;
            }
            distTruth.push_back(idx);
        }
        if (copies > 1) frozen += copies - 1;
        if (!ok) break;
        ++idx;
    }
    reader->close();
    const int rc = _pclose(pipe);
    printf("[合成] 参考 %d 帧 -> 目标 %zu 帧 (丢 %d, 冻结重复 %d), ffmpeg 退出码 %d\n", idx,
           distTruth.size(), dropped, frozen, rc);
    return ok && rc == 0;
}

struct RunResult {
    vt::Summary summary;
    std::vector<std::string> logs;
};

// 完整跑一遍 pipeline 并抓取 summary
RunResult runPipelineFull(const std::string& refPath, const std::string& distPath) {
    RunResult res;
    vt::PipelineConfig cfg;
    cfg.refPath = refPath;
    cfg.distPath = distPath;
    // 内置模型依赖 libvmaf 编译开关, 直接用仓库模型文件更稳
    const std::string model = exeDir() + "/models/vmaf_v0.6.1.json";
    if (fileExists(model)) cfg.vmaf.modelName = model;

    vt::PipelineCallbacks cb;
    cb.onLog = [&](const std::string& level, const std::string& msg) {
        printf("[%s] %s\n", level.c_str(), msg.c_str());
        res.logs.push_back(msg);
    };
    cb.onFinished = [&](const vt::Summary& s) { res.summary = s; };
    vt::Pipeline pipe;
    pipe.run(cfg, cb);
    return res;
}

// 校验一次 pipeline 运行结果
// refM: 参考侧每帧帧号真值, distM: 目标侧每帧帧号真值
void verifyRun(const char* tag, const RunResult& res, const std::vector<int>& refM,
               const std::vector<int>& distM) {
    printf("[校验] %s\n", tag);
    const vt::Summary& s = res.summary;

    bool markerAlignLogged = false;
    for (const auto& l : res.logs)
        if (l.find("使用帧号对齐") != std::string::npos) markerAlignLogged = true;
    CHECK(markerAlignLogged, "日志中未见帧号对齐生效");
    CHECK(s.error.empty(), "pipeline 报错: %s", s.error.c_str());
    CHECK(s.ok, "pipeline 未完成评估");

    // 期望映射: 参考帧 r -> 目标侧首次出现同帧号的位置
    std::vector<int> expected(refM.size(), -1);
    int expectedDropped = 0;
    for (size_t r = 0; r < refM.size(); ++r) {
        if (refM[r] < 0) continue;
        for (size_t d = 0; d < distM.size(); ++d) {
            if (distM[d] == refM[r]) { expected[r] = static_cast<int>(d); break; }
        }
        if (expected[r] < 0) ++expectedDropped;
    }
    // 期望冻结段统计
    int expectedSegs = 0, expectedMaxLen = 0, expectedFrozen = 0;
    for (size_t d = 0; d < distM.size();) {
        size_t e = d + 1;
        while (e < distM.size() && distM[d] >= 0 && distM[e] == distM[d]) ++e;
        if (distM[d] >= 0 && e - d >= 2) {
            ++expectedSegs;
            expectedMaxLen = std::max(expectedMaxLen, static_cast<int>(e - d));
            expectedFrozen += static_cast<int>(e - d - 1);
        }
        d = e;
    }

    CHECK(s.dropped == expectedDropped, "丢帧数: 实际 %d, 期望 %d", s.dropped, expectedDropped);
    CHECK(s.freezeSegments == expectedSegs, "冻结段数: 实际 %d, 期望 %d", s.freezeSegments,
          expectedSegs);
    CHECK(s.freezeMaxLen == expectedMaxLen, "最长冻结: 实际 %d, 期望 %d", s.freezeMaxLen,
          expectedMaxLen);
    CHECK(s.frozen == expectedFrozen, "冻结重复帧数: 实际 %d, 期望 %d", s.frozen, expectedFrozen);

    int refUnrec = 0;
    for (int v : refM)
        if (v < 0) ++refUnrec;
    int distUnrec = 0;
    for (int v : distM)
        if (v < 0) ++distUnrec;
    CHECK(s.markerUnrecognized == refUnrec + distUnrec, "未识别数: 实际 %d, 期望 %d",
          s.markerUnrecognized, refUnrec + distUnrec);

    // 逐帧映射正确率
    int correct = 0, total = 0;
    for (const auto& fr : s.frames) {
        if (fr.refIdx < 0 || static_cast<size_t>(fr.refIdx) >= refM.size()) continue;
        if (refM[static_cast<size_t>(fr.refIdx)] < 0) continue; // 参考侧未识别的帧不校验
        ++total;
        const int exp = expected[static_cast<size_t>(fr.refIdx)];
        if (exp < 0) {
            if (fr.dropped) ++correct;
        } else if (!fr.dropped && fr.distIdx == exp) {
            ++correct;
        } else {
            printf("  [映射错误] 参考帧 %d -> 目标 %d, 期望 %d\n", fr.refIdx, fr.distIdx, exp);
        }
    }
    printf("  映射正确率: %d/%d (%.1f%%)\n", correct, total,
           total > 0 ? 100.0 * correct / total : 0.0);
    CHECK(correct == total, "映射正确率未达 100%%");
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) g_root = argv[1];
    const std::string tmpDir = g_root + "/testdata/tmp";
    std::error_code ec;
    std::filesystem::create_directories(tmpDir, ec);

    const std::string refMarked = tmpDir + "/ref_marked.mp4";
    const std::string distSim = tmpDir + "/dist_sim.mp4";
    const std::string shotsDir = tmpDir + "/shots";

    // ---- 1. 烧码 ----
    printf("==== 1. marker_burn 烧码 ====\n");
    {
        std::string cmd = "\"\"" + exeDir() + "/marker_burn.exe\" \"" + g_root +
                          "/testdata/ref.mp4\" \"" + refMarked + "\" --crf 16\"";
        const int rc = system(cmd.c_str());
        if (rc != 0) {
            printf("[错误] marker_burn 失败, 退出码 %d\n", rc);
            return 1;
        }
    }

    // ---- 2. 合成目标视频 (丢帧+冻结+24fps) ----
    printf("==== 2. 合成目标视频 ====\n");
    std::vector<int> distTruth;
    if (!synthesizeDist(refMarked, distSim, distTruth)) return 1;

    // ---- 真值扫描: 帧号标记逐帧真值 ----
    printf("==== 真值扫描 ====\n");
    const std::vector<int> refM = scanMarkers(refMarked);
    const std::vector<int> distM = scanMarkers(distSim);
    CHECK(refM.size() == 300, "参考帧数: %zu, 期望 300", refM.size());
    {
        int bad = 0;
        for (size_t i = 0; i < refM.size(); ++i)
            if (refM[i] != static_cast<int>(i)) ++bad;
        CHECK(bad == 0, "参考视频 %d 帧帧号与位置不符", bad);
    }
    CHECK(distM.size() == distTruth.size(), "目标帧数: %zu, 期望 %zu", distM.size(),
          distTruth.size());
    {
        int bad = 0;
        for (size_t i = 0; i < distM.size() && i < distTruth.size(); ++i)
            if (distM[i] != distTruth[i]) ++bad;
        CHECK(bad == 0, "目标视频 %d 帧帧号与合成真值不符", bad);
    }

    // ---- 3. 视频对视频: 帧号对齐 ----
    printf("==== 3. pipeline (视频 vs 视频) ====\n");
    verifyRun("视频输入", runPipelineFull(refMarked, distSim), refM, distM);

    // ---- 4. 截图文件夹输入 ----
    printf("==== 4. pipeline (视频 vs PNG 截图文件夹) ====\n");
    std::filesystem::create_directories(shotsDir, ec);
    {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "\"\"%s\" -y -i \"%s\" -start_number 1 \"%s/%%05d.png\"\"",
                 findFfmpeg().c_str(), distSim.c_str(), shotsDir.c_str());
        printf("[命令] %s\n", cmd);
        const int rc = system(cmd);
        CHECK(rc == 0, "PNG 导出失败, 退出码 %d", rc);
    }
    const std::vector<int> shotsM = scanMarkers(shotsDir);
    CHECK(shotsM == distM, "PNG 序列帧号与目标视频不一致 (%zu vs %zu 帧)", shotsM.size(),
          distM.size());
    verifyRun("截图文件夹输入", runPipelineFull(refMarked, shotsDir), refM, distM);

    printf("==== %s ====\n", g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}
