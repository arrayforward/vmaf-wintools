#include "crash_log.h"

#include <QDateTime>
#include <QtGlobal>

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

QString logPath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    const QString p = QString::fromWCharArray(buf);
    const int pos = qMax(p.lastIndexOf('/'), p.lastIndexOf('\\'));
    return (pos >= 0 ? p.left(pos + 1) : QString()) + "vmaf_tool_crash.log";
}

void writeLog(const QString& text) {
    FILE* fp = fopen(logPath().toLocal8Bit().constData(), "a");
    if (!fp) fp = stderr;
    fprintf(fp, "%s\n", text.toUtf8().constData());
    if (fp != stderr) fclose(fp);
}

LONG WINAPI crashHandler(PEXCEPTION_POINTERS ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x40010006 || code == 0x4001000a) // OutputDebugString 通知, 放行
        return EXCEPTION_CONTINUE_SEARCH;
    writeLog(QStringLiteral("\n!!!! 崩溃: exception code 0x%1 at %2")
                 .arg(code, 8, 16, QLatin1Char('0'))
                 .arg(reinterpret_cast<quintptr>(ep->ExceptionRecord->ExceptionAddress), 0, 16));
    void* stack[64];
    const USHORT n = RtlCaptureStackBackTrace(0, 64, stack, nullptr);
    for (USHORT i = 0; i < n; ++i) {
        const DWORD64 addr = reinterpret_cast<DWORD64>(stack[i]);
        HMODULE mod = nullptr;
        wchar_t modName[MAX_PATH] = L"?";
        DWORD64 base = 0;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(addr), &mod)) {
            GetModuleFileNameW(mod, modName, MAX_PATH);
            base = reinterpret_cast<DWORD64>(mod);
        }
        writeLog(QStringLiteral("  #%1 %2+0x%3")
                     .arg(i)
                     .arg(QString::fromWCharArray(modName))
                     .arg(addr - base, 0, 16));
    }
    return EXCEPTION_CONTINUE_SEARCH; // 交给系统默认处理 (WER)
}

void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    // Qt 致命消息 (如 "QThread: Destroyed while thread is still running" 的 qFatal)
    // 不触发 SEH, VEH 抓不到, 必须在这里落盘
    const QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") + " [" +
                         (type == QtFatalMsg ? QStringLiteral("FATAL")
                                             : QStringLiteral("Qt")) + "] " + msg;
    writeLog(line);
    if (type == QtFatalMsg) {
        fprintf(stderr, "%s\n", line.toUtf8().constData());
        fflush(stderr); // qFatal 随后由 Qt 自身 abort, 不改变终止行为
    }
}

} // namespace

void installCrashLogging() {
    AddVectoredExceptionHandler(1, crashHandler);
    qInstallMessageHandler(messageHandler);
}

#else // !_WIN32

void messageHandler(QtMsgType, const QMessageLogContext&, const QString&) {}
void installCrashLogging() { qInstallMessageHandler(messageHandler); }

#endif
