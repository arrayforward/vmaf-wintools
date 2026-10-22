// double_eval_test: 同进程连续多轮 Pipeline 评估 (定位第二轮卡死是否 libvmaf 二次 init 问题)
// 用法: double_eval_test <ref> <dist> [model.json] [--threaded] [--rounds N] [--threads T]
// --threaded: 每轮在独立 std::thread 里执行 (模拟 GUI worker 线程)
#include "core/pipeline.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

void runOnce(int round, const char* ref, const char* dist, const char* model, int threads) {
    printf("==== 第 %d 轮开始 ====\n", round);
    fflush(stdout);
    vt::PipelineConfig cfg;
    cfg.refPath = ref;
    cfg.distPath = dist;
    if (model) cfg.vmaf.modelName = model;
    cfg.vmaf.threads = threads;
    vt::PipelineCallbacks cb;
    cb.onLog = [](const std::string& level, const std::string& msg) {
        printf("[%s] %s\n", level.c_str(), msg.c_str());
        fflush(stdout);
    };
    cb.onFinished = [round](const vt::Summary& s) {
        printf("==== 第 %d 轮结束: ok=%d mean=%.3f psnr=%.2f err=%s\n", round, s.ok, s.mean,
               s.psnrMean, s.error.c_str());
        fflush(stdout);
    };
    vt::Pipeline pipe;
    pipe.run(cfg, cb);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr,
                "用法: %s <ref> <dist> [model.json] [--threaded] [--rounds N] [--threads T]\n",
                argv[0]);
        return 2;
    }
    const char* model = argc > 3 && argv[3][0] != '-' ? argv[3] : nullptr;
    bool threaded = false;
    int rounds = 2, threads = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--threaded") == 0) threaded = true;
        else if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc) rounds = atoi(argv[++i]);
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) threads = atoi(argv[++i]);
    }
    for (int round = 1; round <= rounds; ++round) {
        if (threaded) {
            std::thread t(runOnce, round, argv[1], argv[2], model, threads);
            t.join();
        } else {
            runOnce(round, argv[1], argv[2], model, threads);
        }
    }
    return 0;
}


