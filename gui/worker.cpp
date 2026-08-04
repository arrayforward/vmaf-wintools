#include "worker.h"

EvalWorker::EvalWorker(QObject* parent) : QThread(parent) {}

void EvalWorker::configure(const vt::PipelineConfig& cfg) { cfg_ = cfg; }

void EvalWorker::requestStop() { pipeline_.stopRequested.store(true); }

static QImage rgbToImage(const std::vector<uint8_t>& rgb, int w, int h) {
    if (rgb.empty() || w <= 0 || h <= 0) return QImage();
    return QImage(rgb.data(), w, h, w * 3, QImage::Format_RGB888).copy();
}

void EvalWorker::run() {
    vt::PipelineCallbacks cb;
    int frameCounter = 0;

    cb.onLog = [this](const std::string& level, const std::string& msg) {
        emit logMessage(QString::fromStdString(level), QString::fromStdString(msg));
    };
    cb.onProgress = [this](int done, int total) { emit progressChanged(done, total); };
    cb.onFrame = [this, &frameCounter](const vt::FrameResult& fr) {
        emit frameReady(frameCounter++, fr.refIdx, fr.distIdx, fr.dropped, fr.scoreValid, fr.score,
                        rgbToImage(fr.rgbRef, fr.thumbW, fr.thumbH),
                        rgbToImage(fr.rgbDist, fr.thumbW, fr.thumbH));
    };
    cb.onFinished = [this](const vt::Summary& s) {
        emit evalFinished(s.ok, QString::fromStdString(s.error), s.mean, s.harmonicMean,
                          s.minScore, s.maxScore, s.matched, s.dropped, s.frozen);
    };

    pipeline_.run(cfg_, cb);
}
