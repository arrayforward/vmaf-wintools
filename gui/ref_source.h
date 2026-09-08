#pragma once

#include <QString>

// 参考片源文件名 (与 Android APK 内置片源同一份)
extern const char* const kRefSourceName;

// 自动解析参考片源路径, 搜索顺序:
//   exe 同级 → exe 同级 testdata/ → exe 上两级 testdata/ (开发环境 build/Release)
// 找不到返回空串
QString resolveRefSource();
