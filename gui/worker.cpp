#include "worker.h"

#include "../core/jank.h"
#include "../core/marker.h"
#include "../core/video_reader.h"

EvalWorker::EvalWorker(QObject* parent) : QThread(parent) {
    qRegisterMetaType<QVector<EvalFrameEntry>>();
}

void EvalWorker::configure(const vt::PipelineConfig& cfg) { cfg_ = cfg; }

void EvalWorker::requestStop() { pipeline_.stopRequested.store(true); }

void EvalWorker::run() {
    vt::PipelineCallbacks cb;

    cb.onLog = [this](const std::string& level, const std::string& msg) {
        emit logMessage(QString::fromStdString(level), QString::fromStdString(msg));
    };
    cb.onProgress = [this](int done, int total) { emit progressChanged(done, total); };
    // onFrame 不再转发: 该 libvmaf 构建 flush 前不可取分, GUI 图表/导出统一走
    // evalFinished 携带的全量结果
    cb.onFinished = [this](const vt::Summary& s) {
        QVector<EvalFrameEntry> frames;
        frames.reserve(static_cast<int>(s.frames.size()));
        for (const auto& fr : s.frames)
            frames.append({fr.refIdx, fr.distIdx, fr.dropped, fr.scoreValid, fr.score, fr.psnr,
                           fr.psnrY, fr.psnrU, fr.psnrV});
        emit evalFinished(s.ok, QString::fromStdString(s.error), s.mean, s.harmonicMean,
                          s.minScore, s.maxScore, s.psnrMean, s.matched, s.dropped, s.frozen,
                          s.markerUnrecognized, s.markerCoverage, s.freezeSegments,
                          s.freezeMaxLen, frames);
    };

    pipeline_.run(cfg_, cb);

    // 测试慢机器收尾时序: VMAF_TEST_SLOW_FINISH=秒数 时在 emit evalFinished 之后
    // 模拟耗时清理 (QThread 对象仍存活, 线程未退出)
    static const int slowFinishSec = qgetenv("VMAF_TEST_SLOW_FINISH").toInt();
    if (slowFinishSec > 0) QThread::sleep(slowFinishSec);
}

// ---- JankWorker ----

JankWorker::JankWorker(QObject* parent) : QThread(parent) {
    qRegisterMetaType<QVector<JankSegEntry>>();
}

void JankWorker::configure(const QString& path) { path_ = path.toStdString(); }

void JankWorker::requestStop() { stopRequested_.store(true); }

void JankWorker::run() {
    QVector<JankSegEntry> segs;
    auto finish = [&](bool ok, const QString& error, const vt::JankReport& rep) {
        emit jankFinished(ok, error, rep.captureFps, rep.durationMs, rep.totalFrames,
                          rep.unrecognized, rep.avgFrameMs, rep.maxFrameMs, rep.jankCount,
                          rep.bigJankCount, rep.jankRate, rep.bigJankRate, rep.jankPer10Min, segs);
    };

    std::string err;
    auto reader = vt::createReader(path_, "", 0.0, err);
    if (!reader || !reader->open(err)) {
        finish(false, QString::fromStdString(err), {});
        return;
    }
    const double fps = reader->fps();
    const int64_t total = reader->totalFrames();
    emit logMessage("info", tr("录屏: %1x%2 @ %3fps")
                              .arg(reader->width())
                              .arg(reader->height())
                              .arg(fps, 0, 'f', 2));

    // 逐帧解码 QR 帧号 (-1 = 未识别)
    std::vector<int> indices;
    vt::Frame f;
    while (!stopRequested_.load() && reader->readFrame(f)) {
        vt::MarkerInfo mi;
        if (vt::decodeMarker(f, mi) && mi.valid)
            indices.push_back(mi.frameIndex);
        else
            indices.push_back(-1);
        if (indices.size() % 30 == 0)
            emit progressChanged(static_cast<int>(indices.size()),
                                 total > 0 ? static_cast<int>(total) : 0);
    }
    reader->close();

    if (stopRequested_.load()) {
        finish(false, tr("已取消"), {});
        return;
    }
    if (indices.empty()) {
        finish(false, tr("视频无帧"), {});
        return;
    }
    // 识别率过低: 不是烧码片源录制
    int valid = 0;
    for (const int v : indices)
        if (v >= 0) ++valid;
    if (valid * 5 < static_cast<int>(indices.size()) * 4) {
        finish(false, tr("该视频未检测到足够的帧号标记 (识别率 %1%, 需 >= 80%), "
                         "请确认录屏内容为 marker_burn 烧码片源")
                          .arg(100.0 * valid / indices.size(), 0, 'f', 1),
               {});
        return;
    }

    const vt::JankReport rep = vt::analyzeJank(indices, fps);
    segs.reserve(static_cast<int>(rep.segments.size()));
    for (const auto& s : rep.segments)
        segs.append({s.frameIndex, s.displayMs, s.startMs, s.jank, s.bigJank});
    finish(true, QString(), rep);
}
