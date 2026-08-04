#include "main_window.h"

#include "../core/pipeline.h"

#include <QApplication>
#include <QCoreApplication>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

static int runCli(int argc, char* argv[]) {
    // vmaf_tool --cli <ref> <dist> [--model name_or_path] [--align none|green|content]
    //           [--yuv-size 1920x1080] [--threads N]
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    if (argc < 4) {
        fprintf(stderr,
                "用法: vmaf_tool --cli <参考视频> <目标视频> [--model 模型] "
                "[--align none|green|content] [--yuv-size 1920x1080] [--threads N]\n");
        return 1;
    }
    vt::PipelineConfig cfg;
    cfg.refPath = argv[2];
    cfg.distPath = argv[3];
    for (int i = 4; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        const std::string v = argv[i + 1];
        if (k == "--model") cfg.vmaf.modelName = v;
        else if (k == "--align")
            cfg.alignMode = v == "green" ? vt::AlignMode::GreenScreen
                            : v == "none" ? vt::AlignMode::None
                                          : vt::AlignMode::Content;
        else if (k == "--yuv-size") { cfg.yuvRefSize = v; cfg.yuvDistSize = v; }
        else if (k == "--threads") cfg.vmaf.threads = atoi(v.c_str());
    }

    vt::Pipeline pipeline;
    vt::PipelineCallbacks cb;
    cb.onLog = [](const std::string&, const std::string& msg) { printf("%s\n", msg.c_str()); };
    cb.onProgress = [](int done, int total) {
        if (done % 10 == 0 || done == total) {
            printf("\r进度 %d/%d", done, total);
            fflush(stdout);
        }
    };
    cb.onFinished = [](const vt::Summary& s) {
        printf("\n");
        if (!s.ok) {
            fprintf(stderr, "评估失败: %s\n", s.error.c_str());
            return;
        }
        printf("VMAF mean=%.2f harmonic=%.2f min=%.2f max=%.2f\n", s.mean, s.harmonicMean,
               s.minScore, s.maxScore);
        printf("匹配 %d 帧, 丢帧 %d, 冻结 %d\n", s.matched, s.dropped, s.frozen);
        for (const auto& f : s.frames)
            if (f.scoreValid)
                printf("frame %d -> %d  vmaf=%.2f\n", f.refIdx, f.distIdx, f.score);
            else if (f.dropped)
                printf("frame %d  dropped\n", f.refIdx);
    };
    pipeline.run(cfg, cb);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc > 1 && strcmp(argv[1], "--cli") == 0) {
        QCoreApplication app(argc, argv);
        return runCli(argc, argv);
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName("vmaf_tool");
    MainWindow w;
    w.show();
    return app.exec();
}
