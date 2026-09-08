# VMAF Test Player（云侧片源播放器）

云游戏画质评测方案中**云侧环境**的测试片源播放器。点开即全屏循环播放烧录了
QR 帧号标记的测试视频（1080p60，30~60 秒），端侧通过 adb 截图采集画面做画质比对。

设计目标：工业化、免操作、防呆。

- 无任何权限（不声明 INTERNET，杜绝任何联网/广告行为）
- 无 UI 控件、无文字，纯黑背景
- 启动自动播放、单视频循环（REPEAT_MODE_ONE）、音量 0
- 沉浸式全屏（sticky immersive）+ 屏幕常亮
- 横屏跟随传感器（sensorLandscape）
- 片源缺失时不崩溃：显示纯黑画面并在 logcat 输出警告（tag: `VmafPlayer`）

技术栈：Kotlin + androidx.media3 (ExoPlayer + UI)，minSdk 26，target/compileSdk 34，
包名 `com.vmaftool.player`。

## 目录结构

```
android_player/
├── settings.gradle.kts
├── build.gradle.kts
├── gradle.properties
├── gradlew / gradlew.bat
├── gradle/wrapper/gradle-wrapper.properties   # Gradle 8.7
└── app/
    ├── build.gradle.kts                        # AGP 8.5.2 + media3 1.4.1
    └── src/main/
        ├── AndroidManifest.xml
        ├── assets/
        │   ├── test_video.mp4                  # ← 测试片源放这里（见下）
        │   └── test_video.README.txt
        ├── java/com/vmaftool/player/MainActivity.kt
        └── res/values/styles.xml               # 纯黑全屏主题
```

## 构建

仓库未提交 gradle-wrapper.jar（避免二进制文件），首次构建时由
Android Studio 自动生成；也可在任意已有 Gradle 工程的 wrapper 上初始化。

### 方法一：Android Studio（推荐）

1. Android Studio → Open → 选择 `android_player/` 目录
2. 首次同步会提示生成/使用 Gradle Wrapper（Gradle 8.7），确认即可
3. Build → Build APK(s)，产物在 `app/build/outputs/apk/debug/app-debug.apk`

### 方法二：命令行

```bash
# 已有 Android SDK（ANDROID_HOME 或 local.properties 指向 SDK）
./gradlew wrapper          # 首次生成 gradle-wrapper.jar（仅需一次）
./gradlew assembleDebug
```

## 安装

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

## 放置/替换测试视频

把烧录了 QR 帧号标记的测试视频重命名为 `test_video.mp4`，放到：

```
app/src/main/assets/test_video.mp4
```

重新构建并安装 APK 即可。文件名必须完全一致（区分大小写）。

## 播放确认

- 肉眼：设备点开 App 应立即全屏播放、无系统栏、无控件、循环不息屏。
- adb 确认：

```bash
# App 在前台
adb shell dumpsys activity activities | grep vmaftool
# 播放日志（片源缺失也会在这里看到警告）
adb logcat -s VmafPlayer
# 确认未申请任何权限
adb shell dumpsys package com.vmaftool.player | grep -A2 permission
```

## 调试用：指定外部视频路径

不改 APK，通过 intent extra `video_path` 指定设备上的文件：

```bash
adb push your_video.mp4 /sdcard/Movies/test_video.mp4
adb shell am start -n com.vmaftool.player/.MainActivity \
    --es video_path /sdcard/Movies/test_video.mp4
```

路径指向的文件不存在时自动回退到 assets 里的 `test_video.mp4`；
两者都没有则纯黑画面 + logcat 警告，不崩溃。

## 采集端提示

App 音量恒为 0、纯黑底、RESIZE_MODE_FIT 居中显示，截图画面即原始片源
内容（黑边属于 FIT 缩放结果）。采集时建议固定设备亮度并关闭自动亮度，
避免截图亮度漂移影响画质比对。
