// marker_burn: 逐帧烧录 QR 帧号标记, 经 ffmpeg 管道输出高质量 mp4 片源
//
// 用法:
//   marker_burn <input> <output.mp4> [--crf N]   烧录 (crf 默认 14)
//   marker_burn --verify <video>                 校验第 0/中间/最后帧的帧号

#include "core/frame.h"
#include "core/marker.h"
#include "core/video_reader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::string exeDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf, n);
    const size_t pos = p.find_last_of("\\/");
    return pos == std::string::npos ? "." : p.substr(0, pos);
#else
    return ".";
#endif
}

bool fileExists(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
}

// 定位仓库内置 ffmpeg.exe
std::string findFfmpeg() {
    const std::string dir = exeDir();
    // exe 同目录 (vmaf_tool 构建时会拷贝一份)
    const std::string local = dir + "/ffmpeg.exe";
    if (fileExists(local)) return local;
    // 相对 exe 定位仓库 third_party (exe 通常在 build/Release/)
    const std::string repo = dir + "/../../third_party/ffmpeg/ffmpeg-master-latest-win64-gpl-shared/bin/ffmpeg.exe";
    if (fileExists(repo)) return repo;
    return "ffmpeg"; // 兜底: PATH
}

bool writePlane(FILE* pipe, const uint8_t* data, int stride, int w, int h) {
    for (int r = 0; r < h; ++r) {
        if (fwrite(data + static_cast<size_t>(r) * stride, 1, w, pipe) != static_cast<size_t>(w))
            return false;
    }
    return true;
}

int burn(const std::string& input, const std::string& output, int crf) {
    std::string err;
    auto reader = vt::createReader(input, "", 0.0, err);
    if (!reader || !reader->open(err)) {
        fprintf(stderr, "[错误] 无法打开输入 %s: %s\n", input.c_str(), err.c_str());
        return 1;
    }

    const int w = reader->width();
    const int h = reader->height();
    const double fps = reader->fps();
    printf("[信息] 输入 %s: %dx%d @ %.6f fps, 总帧数 %lld\n", input.c_str(), w, h, fps,
           static_cast<long long>(reader->totalFrames()));

    char cmd[4096];
    // 最外层再包一对引号: 命令行首尾都是引号时 cmd.exe 会错误地剥掉它们
    snprintf(cmd, sizeof(cmd),
             "\"\"%s\" -y -f rawvideo -pix_fmt yuv420p -s %dx%d -r %.6f -i - "
             "-c:v libx264 -crf %d -preset fast -pix_fmt yuv420p -an \"%s\"\"",
             findFfmpeg().c_str(), w, h, fps, crf, output.c_str());
    printf("[命令] %s\n", cmd);

    FILE* pipe = _popen(cmd, "wb");
    if (!pipe) {
        fprintf(stderr, "[错误] 无法启动 ffmpeg 子进程\n");
        return 1;
    }

    vt::Frame f;
    int idx = 0;
    bool ok = true;
    while (reader->readFrame(f)) {
        if (!vt::burnMarker(f, idx)) {
            fprintf(stderr, "[错误] 第 %d 帧烧录失败\n", idx);
            ok = false;
            break;
        }
        // 按行写入, 尊重 stride (yuv420p 色度平面半分辨率, 奇数尺寸向上取整)
        if (!writePlane(pipe, f.y.data(), f.strideY, w, h) ||
            !writePlane(pipe, f.u.data(), f.strideU, (w + 1) / 2, (h + 1) / 2) ||
            !writePlane(pipe, f.v.data(), f.strideV, (w + 1) / 2, (h + 1) / 2)) {
            fprintf(stderr, "[错误] 第 %d 帧写管道失败\n", idx);
            ok = false;
            break;
        }
        ++idx;
        if (idx % 100 == 0) printf("[进度] 已处理 %d 帧\n", idx);
    }
    reader->close();

    const int rc = _pclose(pipe);
    if (!ok) return 1;
    if (rc != 0) {
        fprintf(stderr, "[错误] ffmpeg 退出码 %d\n", rc);
        return 1;
    }
    printf("[完成] 共烧录 %d 帧 -> %s\n", idx, output.c_str());
    return 0;
}

int verify(const std::string& path) {
    std::string err;
    int64_t total = -1;
    {
        auto r = vt::createReader(path, "", 0.0, err);
        if (!r || !r->open(err)) {
            fprintf(stderr, "[错误] 无法打开 %s: %s\n", path.c_str(), err.c_str());
            return 1;
        }
        // 优先用 totalFrames; 未知则逐帧数一遍
        total = r->totalFrames();
        if (total < 0) {
            vt::Frame f;
            total = 0;
            while (r->readFrame(f)) ++total;
        }
        r->close();
    }
    if (total <= 0) {
        fprintf(stderr, "[错误] 视频无帧\n");
        return 1;
    }
    const int targets[3] = {0, static_cast<int>(total / 2), static_cast<int>(total - 1)};
    printf("[校验] %s: 共 %lld 帧, 检查第 %d/%d/%d 帧\n", path.c_str(),
           static_cast<long long>(total), targets[0], targets[1], targets[2]);

    auto r = vt::createReader(path, "", 0.0, err);
    if (!r || !r->open(err)) {
        fprintf(stderr, "[错误] 无法重新打开 %s: %s\n", path.c_str(), err.c_str());
        return 1;
    }
    bool pass = true;
    int t = 0;
    vt::Frame f;
    int idx = 0;
    while (t < 3 && r->readFrame(f)) {
        if (idx == targets[t]) {
            vt::MarkerInfo info;
            const bool ok = vt::decodeMarker(f, info);
            const bool good = ok && info.valid && info.frameIndex == idx;
            printf("  帧 %5d: 解码 %s, frameIndex=%d (期望 %d) %s\n", idx,
                   ok && info.valid ? "成功" : "失败", info.frameIndex, idx,
                   good ? "[OK]" : "[FAIL]");
            pass &= good;
            ++t;
        }
        ++idx;
    }
    r->close();
    if (t < 3) {
        printf("  [错误] 视频帧数不足, 只读到 %d 帧\n", idx);
        pass = false;
    }
    printf("[校验] %s\n", pass ? "全部通过" : "存在失败");
    return pass ? 0 : 1;
}

// 解析 PNG 文件头拿宽高 (IHDR: 8 字节签名 + 4 字节长度 + "IHDR" + 宽高各 4 字节大端)
bool pngSize(const std::string& path, int& w, int& h) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char hdr[24];
    const size_t n = fread(hdr, 1, sizeof(hdr), f);
    fclose(f);
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n < sizeof(hdr) || memcmp(hdr, sig, 8) != 0 || memcmp(hdr + 12, "IHDR", 4) != 0)
        return false;
    w = (hdr[16] << 24) | (hdr[17] << 16) | (hdr[18] << 8) | hdr[19];
    h = (hdr[20] << 24) | (hdr[21] << 16) | (hdr[22] << 8) | hdr[23];
    return w > 0 && h > 0;
}

// 校验单张 PNG: ffmpeg 子进程转 RGB24 rawvideo, 调 decodeMarkerRgb 解码
int verifyImage(const std::string& path) {
    int w = 0, h = 0;
    if (!pngSize(path, w, h)) {
        fprintf(stderr, "[错误] 无法解析 PNG 头: %s\n", path.c_str());
        return 1;
    }
    printf("[信息] %s: %dx%d\n", path.c_str(), w, h);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "\"\"%s\" -v error -i \"%s\" -f rawvideo -pix_fmt rgb24 -\"",
             findFfmpeg().c_str(), path.c_str());
    FILE* pipe = _popen(cmd, "rb");
    if (!pipe) {
        fprintf(stderr, "[错误] 无法启动 ffmpeg 子进程\n");
        return 1;
    }
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    size_t got = 0;
    while (got < rgb.size()) {
        const size_t n = fread(rgb.data() + got, 1, rgb.size() - got, pipe);
        if (n == 0) break;
        got += n;
    }
    const int rc = _pclose(pipe);
    if (rc != 0 || got != rgb.size()) {
        fprintf(stderr, "[错误] ffmpeg 读取 RGB 失败: 退出码 %d, 读 %zu/%zu 字节\n",
                rc, got, rgb.size());
        return 1;
    }

    vt::MarkerInfo info;
    const bool ok = vt::decodeMarkerRgb(rgb.data(), w, h, w * 3, info);
    printf("[解码] valid=%s frameIndex=%d QR外接框=(%d,%d 边长%d)\n",
           ok && info.valid ? "true" : "false", info.frameIndex,
           info.qrX, info.qrY, info.qrSize);
    return ok && info.valid ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && strcmp(argv[1], "--verify") == 0)
        return verify(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "--verify-image") == 0)
        return verifyImage(argv[2]);

    if (argc < 3) {
        fprintf(stderr, "用法: %s <input> <output.mp4> [--crf N]\n"
                        "      %s --verify <video>\n"
                        "      %s --verify-image <png>\n", argv[0], argv[0], argv[0]);
        return 2;
    }
    int crf = 14;
    for (int i = 3; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--crf") == 0) crf = atoi(argv[i + 1]);
    }
    return burn(argv[1], argv[2], crf);
}
