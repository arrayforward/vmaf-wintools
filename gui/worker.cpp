#include "worker.h"

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
            frames.append({fr.refIdx, fr.distIdx, fr.dropped, fr.scoreValid, fr.score});
        emit evalFinished(s.ok, QString::fromStdString(s.error), s.mean, s.harmonicMean,
                          s.minScore, s.maxScore, s.matched, s.dropped, s.frozen,
                          s.markerUnrecognized, s.markerCoverage, s.freezeSegments,
                          s.freezeMaxLen, frames);
    };

    pipeline_.run(cfg_, cb);
}
