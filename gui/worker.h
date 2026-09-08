#pragma once

#include "../core/pipeline.h"

#include <QMetaType>
#include <QThread>
#include <QVector>

// 逐帧评测结果 (供导出; 与 vt::FrameResult 对应, 去掉缩略图)
struct EvalFrameEntry {
    int refIdx = -1, distIdx = -1;
    bool dropped = false;
    bool scoreValid = false;
    double score = 0;
};
Q_DECLARE_METATYPE(EvalFrameEntry)

class EvalWorker : public QThread {
    Q_OBJECT
public:
    explicit EvalWorker(QObject* parent = nullptr);

    void configure(const vt::PipelineConfig& cfg);
    void requestStop();

signals:
    void logMessage(const QString& level, const QString& msg);
    void progressChanged(int done, int total);
    // frames = Summary.frames 全量 (含 flush 后补齐的分数), 供导出与图表重建
    void evalFinished(bool ok, const QString& error, double mean, double harmonic, double minS,
                      double maxS, int matched, int dropped, int frozen,
                      int markerUnrecognized, double markerCoverage, int freezeSegments,
                      int freezeMaxLen, const QVector<EvalFrameEntry>& frames);

protected:
    void run() override;

private:
    vt::PipelineConfig cfg_;
    vt::Pipeline pipeline_;
};
