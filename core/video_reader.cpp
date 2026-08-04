#include "video_reader.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace vt {

class FFmpegReader : public VideoReader {
public:
    explicit FFmpegReader(std::string path) : path_(std::move(path)) {}

    ~FFmpegReader() override { close(); }

    bool open(std::string& err) override {
        if (avformat_open_input(&fmt_, path_.c_str(), nullptr, nullptr) < 0) {
            err = "无法打开视频: " + path_;
            return false;
        }
        if (avformat_find_stream_info(fmt_, nullptr) < 0) {
            err = "无法读取流信息";
            return false;
        }
        streamIdx_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec_, 0);
        if (streamIdx_ < 0) {
            err = "未找到视频流";
            return false;
        }
        AVStream* st = fmt_->streams[streamIdx_];
        codecCtx_ = avcodec_alloc_context3(codec_);
        if (!codecCtx_ || avcodec_parameters_to_context(codecCtx_, st->codecpar) < 0 ||
            avcodec_open2(codecCtx_, codec_, nullptr) < 0) {
            err = "解码器初始化失败";
            return false;
        }
        w_ = codecCtx_->width;
        h_ = codecCtx_->height;
        fps_ = av_q2d(st->avg_frame_rate);
        if (fps_ <= 0.01) fps_ = av_q2d(st->r_frame_rate);
        if (fps_ <= 0.01) fps_ = 25.0;
        if (st->nb_frames > 0)
            totalFrames_ = st->nb_frames;
        else if (st->duration != AV_NOPTS_VALUE)
            totalFrames_ = static_cast<int64_t>(av_q2d(st->time_base) * st->duration * fps_);
        else if (fmt_->duration != AV_NOPTS_VALUE)
            totalFrames_ = static_cast<int64_t>(fmt_->duration / 1e6 * fps_);

        sws_ = sws_getContext(w_, h_, codecCtx_->pix_fmt, w_, h_, AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) {
            err = "swscale 初始化失败";
            return false;
        }
        pkt_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        return pkt_ && frame_;
    }

    bool readFrame(Frame& f) override {
        while (true) {
            if (!pktSent_) {
                int ret = av_read_frame(fmt_, pkt_);
                if (ret < 0) {
                    // EOF: flush decoder
                    avcodec_send_packet(codecCtx_, nullptr);
                    pktSent_ = true;
                } else {
                    if (pkt_->stream_index != streamIdx_) {
                        av_packet_unref(pkt_);
                        continue;
                    }
                    if (avcodec_send_packet(codecCtx_, pkt_) < 0) {
                        av_packet_unref(pkt_);
                        return false;
                    }
                    av_packet_unref(pkt_);
                    pktSent_ = true;
                }
            }
            int ret = avcodec_receive_frame(codecCtx_, frame_);
            if (ret == AVERROR(EAGAIN)) {
                pktSent_ = false;
                continue;
            }
            if (ret < 0) return false; // EOF

            f.alloc(w_, h_);
            uint8_t* dst[4] = {f.y.data(), f.u.data(), f.v.data(), nullptr};
            int dstStride[4] = {f.strideY, f.strideU, f.strideV, 0};
            sws_scale(sws_, frame_->data, frame_->linesize, 0, h_, dst, dstStride);
            f.ptsMs = frame_->best_effort_timestamp == AV_NOPTS_VALUE
                          ? 0
                          : static_cast<int64_t>(frame_->best_effort_timestamp *
                                                 av_q2d(fmt_->streams[streamIdx_]->time_base) * 1000.0);
            av_frame_unref(frame_);
            pktSent_ = false;
            return true;
        }
    }

    int width() const override { return w_; }
    int height() const override { return h_; }
    double fps() const override { return fps_; }
    int64_t totalFrames() const override { return totalFrames_; }

    void close() override {
        if (sws_) sws_freeContext(sws_);
        sws_ = nullptr;
        if (frame_) av_frame_free(&frame_);
        if (pkt_) av_packet_free(&pkt_);
        if (codecCtx_) avcodec_free_context(&codecCtx_);
        if (fmt_) avformat_close_input(&fmt_);
    }

private:
    std::string path_;
    AVFormatContext* fmt_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    const AVCodec* codec_ = nullptr;
    SwsContext* sws_ = nullptr;
    AVPacket* pkt_ = nullptr;
    AVFrame* frame_ = nullptr;
    int streamIdx_ = -1;
    int w_ = 0, h_ = 0;
    double fps_ = 0;
    int64_t totalFrames_ = -1;
    bool pktSent_ = false;
};

std::unique_ptr<VideoReader> createFFmpegReader(const std::string& path) {
    return std::make_unique<FFmpegReader>(path);
}

std::unique_ptr<VideoReader> createReader(const std::string& path, const std::string& yuvSize,
                                          double yuvFps, std::string& err) {
    const auto dot = path.find_last_of('.');
    const std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    if (ext == "yuv" || ext == "YUV") {
        int w = 0, h = 0;
        if (sscanf_s(yuvSize.c_str(), "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) {
            err = "YUV 文件需要正确的分辨率参数 (如 1920x1080)";
            return nullptr;
        }
        return createYuvReader(path, w, h, yuvFps);
    }
    return createFFmpegReader(path);
}

} // namespace vt
