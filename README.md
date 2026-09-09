# VMAF 画质评估工具

云游戏画质评测工具:片源逐帧烧录 QR 帧号标记,Android 端全屏循环播放,端侧 adb 截图/录屏采集,
Windows 端按帧号精确对齐并做全参考 VMAF 打分(逐帧 + 池化统计)。

## 方案概述

- **片源**:marker_burn 逐帧烧录 QR 帧号标记(规范见 [docs/帧号标记规范.md](docs/帧号标记规范.md))
- **云侧**:android_player/ 专用 APK,无任何权限、全屏循环播放片源
- **采集**:adb 定时截图(PNG 序列文件夹)或端侧录屏
- **评测**:QR 帧号精确对齐(天然处理丢帧/冻结/乱序/帧率不一致)
  + QR 外接框反推视频区域自动裁剪(含黑边吸附细化)
  + libvmaf 打分;丢帧/冻结由帧号映射单独统计
- **卡顿分析**:对播放画面的录屏逐帧解帧号,按 PerfDog 标准统计
  Jank/Big Jank 次数、卡顿率、Jank/10min(GUI 卡顿分析 Tab / jank_cli)
- **参考免选择**:评测只需选目标(截图文件夹或录屏),参考片源自动解析 `ai_game_1080p60.mp4`

## 快速上手

1. **装 APK**:构建安装 android_player(见 [android_player/README.md](android_player/README.md)),
   烧码片源命名 `test_video.mp4` 放入 `app/src/main/assets/`
2. **播放**:设备上打开 App,自动全屏循环播放
3. **采集**:vmaf_tool 的"采集"Tab 经 adb 定时截图(输出 PNG 序列文件夹),或端侧录屏
4. **评测**:vmaf_tool 的"画质评估"Tab 选目标(截图文件夹/录屏)开始评估;
   对齐与裁剪全自动,完成后可导出 JSON/CSV
5. **卡顿分析**(可选):vmaf_tool 的"卡顿分析"Tab 选录屏视频,按 PerfDog 标准输出
   Jank/Big Jank/卡顿率,可导出 CSV

```bat
:: GUI
build\Release\vmaf_tool.exe

:: CLI (参考可缺省, 自动解析 ai_game_1080p60.mp4)
vmaf_tool.exe --cli [参考视频] <目标视频或截图文件夹> [--model 模型] [--threads N]

:: 独立命令行 (帧号对齐 + 自动裁剪)
eval_cli.exe <ref视频> <dist视频或截图文件夹> [model路径]

:: 卡顿率分析 (PerfDog 标准)
jank_cli.exe <录屏视频> [--export-csv out.csv]
```

## 目录结构

| 目录 | 说明 |
|---|---|
| `core/` | 核心库(纯 C++,无 Qt 依赖):解码、QR 帧号、对齐、自动裁剪、libvmaf 封装、Pipeline |
| `gui/` | Qt6 桌面端:采集 / 卡顿分析 / 画质评估 / 视频预处理 四个 Tab |
| `tools/` | `eval_cli`(命令行评测)、`marker_burn`(片源烧码/校验)、`jank_cli`(卡顿率分析) |
| `android_player/` | 云侧测试片源播放器(Android APK) |
| `models/` | VMAF 模型 JSON(随 exe 分发) |
| `docs/` | 架构/流程/使用/交互/帧号标记规范文档 |
| `tests/` | 帧号对齐与标记的自动化测试 |
| `third_party/` | FFmpeg、libvmaf、quirc、qrcodegen |

## 构建

### Windows 桌面端

依赖:VS 2022 (C++ 工作负载)、Qt 6 (msvc2022_64)、FFmpeg 开发包、libvmaf

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH=D:\tools\Qt\6.8.3\msvc2022_64 ^
  -DFFMPEG_ROOT=<ffmpeg解压目录> ^
  -DLIBVMAF_ROOT=<libvmaf安装目录> ^
  -DMINGW_BIN=<mingw64\bin>
cmake --build build --config Release
```

产物 `build/Release/` 整目录可直接分发。详见 [docs/架构设计.md](docs/架构设计.md#构建与部署)。

### Android 播放器

Android Studio 打开 `android_player/` 直接 Build APK;或命令行
`./gradlew wrapper && ./gradlew assembleDebug`。详见 [android_player/README.md](android_player/README.md)。

## 验证结论

真机本地播放链路端到端实测:QR 帧号识别率 100%、逐帧对齐正确率 100%。
VMAF 绝对分受显示缩放滤波影响(竖屏 0.56x 显示下约 75 为测量上限),
基准验证建议用横屏或分辨率匹配的设备。详见 [docs/使用说明.md](docs/使用说明.md#5-结果解读)。

## 文档

| 文档 | 说明 |
|---|---|
| [docs/使用说明.md](docs/使用说明.md) | 安装、界面操作、测试流程 |
| [docs/架构设计.md](docs/架构设计.md) | 模块划分、技术选型、数据流 |
| [docs/主要流程.md](docs/主要流程.md) | 评估流水线、对齐算法、预处理流程 |
| [docs/交互文档.md](docs/交互文档.md) | 界面布局、控件交互、信号流 |
| [docs/帧号标记规范.md](docs/帧号标记规范.md) | QR 帧号标记定义与解码流程 |
| [android_player/README.md](android_player/README.md) | 云侧播放器构建/安装/使用 |
