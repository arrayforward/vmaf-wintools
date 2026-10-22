// eval_gui_test: 复现 GUI 评测收尾崩溃 (offscreen 或真窗口)
//
// 用法: eval_gui_test <目标视频或截图文件夹> [超时秒, 默认 240] [--offscreen]
// 参考片源由 gui/ref_source.cpp 自动解析 (exe 上两级 testdata 命中仓库)。
// 崩溃诊断 (VEH + Qt 消息日志) 与正式程序共用 gui/crash_log。

#include "../gui/crash_log.h"
#include "../gui/eval_tab.h"
#include "../gui/main_window.h"

#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

// 卡死定位: 枚举本进程线程, 挂起后读取 RIP 及所属模块 (无需外部调试器)
static void dumpThreads() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te = {};
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId()) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                  THREAD_QUERY_INFORMATION,
                              FALSE, te.th32ThreadID);
        if (!h) continue;
        if (te.th32ThreadID != GetCurrentThreadId()) {
            SuspendThread(h);
            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(h, &ctx)) {
                HMODULE mod = nullptr;
                wchar_t modName[MAX_PATH] = L"?";
                DWORD64 base = 0;
                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       reinterpret_cast<LPCWSTR>(ctx.Rip), &mod)) {
                    GetModuleFileNameW(mod, modName, MAX_PATH);
                    base = reinterpret_cast<DWORD64>(mod);
                }
                fprintf(stderr, "[thread %lu] RIP=%llx %S+0x%llx\n", te.th32ThreadID,
                        static_cast<unsigned long long>(ctx.Rip), modName,
                        static_cast<unsigned long long>(ctx.Rip - base));
            }
            ResumeThread(h);
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    fflush(stderr);
}
#endif

int main(int argc, char** argv) {
    installCrashLogging();
    if (argc < 2) {
        fprintf(stderr, "用法: %s <目标视频或截图文件夹> [超时秒] [--offscreen]\n", argv[0]);
        return 2;
    }
    // --offscreen: 无显示运行; 默认真实 qwindows 窗口 (复现真实 GUI 环境)
    bool offscreen = false;
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "--offscreen") == 0) offscreen = true;
    const int timeoutSec = argc > 2 && atoi(argv[2]) > 0 ? atoi(argv[2]) : 240;
    if (offscreen) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    MainWindow w;
    w.show();
    auto* tab = w.findChild<EvalTab*>();
    if (!tab) {
        fprintf(stderr, "找不到 EvalTab\n");
        return 2;
    }

    // 结果栏在 startEvaluation 之前定位 (文本 "尚未评估")
    QLabel* resultLabel = nullptr;
    for (auto* l : tab->findChildren<QLabel*>())
        if (l->text().contains(QStringLiteral("尚未评估"))) resultLabel = l;

    // Windows CRT argv 为系统代码页 (GBK), 中文路径需按本地编码解码
    tab->setDistInput(QString::fromLocal8Bit(argv[1]));
    // 实验: 设置 VMAF_TEST_THREADS=N 时把线程数 spinbox 设为 N (0=自动)
    const int testThreads = qgetenv("VMAF_TEST_THREADS").toInt();
    if (testThreads > 0)
        for (auto* s : tab->findChildren<QSpinBox*>()) s->setValue(testThreads);
    // 心跳: 区分 GUI 主线程死锁 (心跳停止) 与 worker 卡死 (心跳继续但进度不动)
    auto* progress = tab->findChild<QProgressBar*>();
    QTimer heartbeat;
    int hbCount = 0;
    QObject::connect(&heartbeat, &QTimer::timeout, [progress, &hbCount] {
        ++hbCount;
        if (progress) printf("[hb] progress=%d/%d\n", progress->value(), progress->maximum());
        fflush(stdout);
#ifdef _WIN32
        if (hbCount % 6 == 0) dumpThreads(); // 每 30s 采样一次线程 RIP
#endif
    });
    heartbeat.start(5000);
    // 模拟采集 Tab 联动: 直接发 sendToEval (与 MainWindow 连接路径一致)
    for (auto* c : w.findChildren<QWidget*>()) {
        if (QString::fromLatin1(c->metaObject()->className()) == "CaptureTab") {
            QMetaObject::invokeMethod(c, "sendToEval",
                                      Q_ARG(QString, QString::fromLocal8Bit(argv[1])));
            break;
        }
    }
    tab->startEvaluation();
    QElapsedTimer timer;
    timer.start();
    int rc = 1;
    int phase = 0; // 0=等第一轮完成, 1=等第二轮完成
    qint64 phaseStart = timer.elapsed();
    while (timer.elapsed() < timeoutSec * 1000LL) {
        app.processEvents(QEventLoop::AllEvents, 200);
        if (!resultLabel) continue;
        const QString t = resultLabel->text();
        if (t.contains(QStringLiteral("VMAF mean"))) {
            printf("[harness] 第 %d 轮评测完成: %s\n", phase + 1, t.toUtf8().constData());
            if (phase == 0) {
                phase = 1;
                phaseStart = timer.elapsed();
                // 2 秒后发起第二轮 (覆盖 worker 重建/图表二次重建路径)
                QTimer::singleShot(2000, tab, [&tab] { tab->startEvaluation(); });
                // 等结果栏变回 "评估中…" 再进入完成检测
                while (timer.elapsed() - phaseStart < 10000) {
                    app.processEvents(QEventLoop::AllEvents, 100);
                    if (resultLabel->text().contains(QStringLiteral("评估中"))) break;
                }
                continue;
            }
            rc = 0;
            break;
        }
        if (t.contains(QStringLiteral("评估失败"))) {
            printf("[harness] 评测失败: %s\n", t.toUtf8().constData());
            rc = 1;
            break;
        }
    }
    if (timer.elapsed() >= timeoutSec * 1000LL) {
        printf("[harness] 超时, 结果栏: %s\n",
               resultLabel ? resultLabel->text().toUtf8().constData() : "(无)");
#ifdef _WIN32
        dumpThreads();
#endif
        // .dump 日志区内容辅助定位
        for (auto* l : tab->findChildren<QLabel*>())
            printf("[label] %s\n", l->text().toUtf8().constData());
        for (auto* p : tab->findChildren<QPlainTextEdit*>())
            printf("[log]\n%s\n", p->toPlainText().toUtf8().constData());
    }
    fflush(stdout);
    return rc;
}
