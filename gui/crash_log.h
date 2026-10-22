#pragma once

// 安装崩溃诊断: SEH 崩溃栈 + Qt qWarning/qFatal 消息,
// 均追加写到 exe 旁 vmaf_tool_crash.log (带时间戳)。
void installCrashLogging();
