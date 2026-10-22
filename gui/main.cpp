#include "main_window.h"
#include "crash_log.h"
#include "ref_source.h"

#include "../core/pipeline.h"

#include <QApplication>
#include <QCoreApplication>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Windows 下命令行参数为本地代码页 (GBK), 转成 UTF-8 供 FFmpeg 打开中文路径
static std::vector<std::string> g_utf8Args;
static std::vector<char*> g_argvPtrs;

static void convertWinArgs(int& argc, char**& argv) {
#ifdef _WIN32
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv) return;
    g_utf8Args.clear();
    for (int i = 0; i < wargc; ++i) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len ? len - 1 : 0, '\0');
        if (len > 1)
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), len, nullptr, nullptr);
        g_utf8Args.push_back(std::move(s));
    }
    LocalFree(wargv);
    g_argvPtrs.clear();
    for (auto& s : g_utf8Args) g_argvPtrs.push_back(s.data());
    argc = wargc;
    argv = g_argvPtrs.data();
#else
    (void)argc;
    (void)argv;
#endif
}

static int runCli(int argc, char* argv[]) {
    // vmaf_tool --cli [参考视频] <目标视频> [--model name_or_path] [--yuv-size 1920x1080]
    //           [--threads N]
    // 参考视频缺省时自动解析 ai_game_1080p60.mp4 (见 ref_source.h)
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    vt::PipelineConfig cfg;
    std::vector<std::string> positional;
    for (int i = 2; i < argc; ++i) {
        const std::string k = argv[i];
        if (k.rfind("--", 0) != 0) {
            positional.push_back(k);
            continue;
        }
        if (i + 1 >= argc) break;
        const std::string v = argv[++i];
        if (k == "--model") cfg.vmaf.modelName = v;
        else if (k == "--yuv-size") { cfg.yuvRefSize = v; cfg.yuvDistSize = v; }
        else if (k == "--threads") cfg.vmaf.threads = atoi(v.c_str());
    }
    if (positional.size() == 2) {
        cfg.refPath = positional[0];
        cfg.distPath = positional[1];
    } else if (positional.size() == 1) {
        cfg.distPath = positional[0];
    } else {
        fprintf(stderr,
                "用法: vmaf_tool --cli [参考视频] <目标视频> [--model 模型] "
                "[--yuv-size 1920x1080] [--threads N]\n");
        return 1;
    }
    if (cfg.refPath.empty()) {
        const QString ref = resolveRefSource();
        if (ref.isEmpty()) {
            fprintf(stderr, "错误: 未找到参考片源 %s, 请将其放在程序目录\n", kRefSourceName);
            return 1;
        }
        cfg.refPath = ref.toStdString();
        printf("参考片源: %s\n", cfg.refPath.c_str());
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
    installCrashLogging();
    convertWinArgs(argc, argv);
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
