# VMAF 画质评估工具

基于 Netflix libvmaf 的 C++/Qt6 画质评估工具,面向"云侧播放 → 端侧录屏"的直播画质测试场景。

## 特性

- 全参考 VMAF 评分(逐帧 + 池化统计),支持丢帧/降帧/冻结检测
- 三种帧对齐模式:绿屏标定 / 内容自动匹配(NCC) / 不对齐
- 所见即所得的拖拽裁剪(默认中心 1/2)
- 逐帧 VMAF 曲线,点击跳转任意帧对双画面回看
- 预处理:片头插入 5 秒绿屏 + 转码(分辨率/码率,默认 1080p@5Mbps)
- GUI + CLI 双模式,JSON/CSV 结果导出

## 快速开始

```bat
:: GUI
build\Release\vmaf_tool.exe

:: CLI
vmaf_tool.exe --cli ref.mp4 dist.mp4 --model models/vmaf_v0.6.1.json --align content
```

## 文档

| 文档 | 说明 |
|---|---|
| [docs/使用说明.md](docs/使用说明.md) | 安装、界面操作、测试流程 |
| [docs/架构设计.md](docs/架构设计.md) | 模块划分、技术选型、数据流 |
| [docs/主要流程.md](docs/主要流程.md) | 评估流水线、对齐算法、预处理流程 |
| [docs/交互文档.md](docs/交互文档.md) | 界面布局、控件交互、信号流 |

## 构建

依赖:VS 2022 (C++ 工作负载)、Qt 6 (msvc2022_64)、FFmpeg 开发包、libvmaf

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH=D:\tools\Qt\6.8.3\msvc2022_64 ^
  -DFFMPEG_ROOT=<ffmpeg解压目录> ^
  -DLIBVMAF_ROOT=<libvmaf安装目录> ^
  -DMINGW_BIN=<mingw64\bin>
cmake --build build --config Release
```

详见 [docs/架构设计.md](docs/架构设计.md#构建与部署)。

## 验证结果

| 场景 | VMAF mean |
|---|---|
| 同视频自比对 | 99.3 |
| crf34 同帧率 | 87.0 |
| crf34 + 30fps→24fps 丢帧(内容匹配) | 87.8 |
| 片头 5s 绿屏 + 丢帧(绿屏标定) | 87.8 |
