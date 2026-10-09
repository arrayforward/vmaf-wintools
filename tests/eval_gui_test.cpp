// eval_gui_test: offscreen 复现 GUI 评测收尾崩溃
//
// 用法: eval_gui_test <目标视频或截图文件夹> [超时秒, 默认 240]
// 参考片源由 gui/ref_source.cpp 自动解析 (exe 上两级 testdata 命中仓库)。
// 崩溃时通过 VEH + dbghelp 打印调用栈。

#include "../gui/eval_tab.h"
#include "../gui/main_window.h"

#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>

#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static LONG WINAPI crashDump(PEXCEPTION_POINTERS ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    // 只处理致命异常; 0x4001xxxx 是无调试器时 OutputDebugString 的调试输出通知, 放行
    if (code == 0x40010006 || code == 0x4001000a) return EXCEPTION_CONTINUE_SEARCH;
    fprintf(stderr, "\n!!!! 崩溃: exception code 0x%08lx at %p\n", code,
            ep->ExceptionRecord->ExceptionAddress);
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);
    void* stack[64];
    const USHORT n = RtlCaptureStackBackTrace(0, 64, stack, nullptr);
    SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(_alloca(sizeof(SYMBOL_INFO) + 256));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(line);
    for (USHORT i = 0; i < n; ++i) {
        const DWORD64 addr = reinterpret_cast<DWORD64>(stack[i]);
        // 模块名 + 偏移 (无 pdb 的 DLL 也能定位)
        HMODULE mod = nullptr;
        wchar_t modName[MAX_PATH] = L"?";
        DWORD64 base = 0;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(addr), &mod)) {
            GetModuleFileNameW(mod, modName, MAX_PATH);
            base = reinterpret_cast<DWORD64>(mod);
        }
        QString desc = QString::fromWCharArray(modName) + "+" +
                       QString::number(addr - base, 16);
        if (SymFromAddr(proc, addr, nullptr, sym) && sym->NameLen > 1) {
            desc += QString::fromLatin1("  ") + QString::fromLatin1(sym->Name);
            DWORD disp = 0;
            if (SymGetLineFromAddr64(proc, addr, &disp, &line))
                desc += QString::fromLatin1("  %1:%2").arg(line.FileName).arg(line.LineNumber);
        }
        fprintf(stderr, "  #%02d %s\n", i, desc.toUtf8().constData());
    }
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    AddVectoredExceptionHandler(1, crashDump);
#endif
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

    tab->setDistInput(QString::fromUtf8(argv[1]));
    // 模拟采集 Tab 联动: 直接发 sendToEval (与 MainWindow 连接路径一致)
    for (auto* c : w.findChildren<QWidget*>()) {
        if (QString::fromLatin1(c->metaObject()->className()) == "CaptureTab") {
            QMetaObject::invokeMethod(c, "sendToEval", Q_ARG(QString, QString::fromUtf8(argv[1])));
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
        // .dump 日志区内容辅助定位
        for (auto* l : tab->findChildren<QLabel*>())
            printf("[label] %s\n", l->text().toUtf8().constData());
        for (auto* p : tab->findChildren<QPlainTextEdit*>())
            printf("[log]\n%s\n", p->toPlainText().toUtf8().constData());
    }
    fflush(stdout);
    return rc;
}
