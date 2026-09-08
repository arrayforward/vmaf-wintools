#pragma once

#include "../core/pipeline.h"

#include <QImage>
#include <QThread>

class EvalWorker : public QThread {
    Q_OBJECT
public:
    explicit EvalWorker(QObject* parent = nullptr);

    void configure(const vt::PipelineConfig& cfg);
    void requestStop();

signals:
    void logMessage(const QString& level, const QString& msg);
    void progressChanged(int done, int total);
    // idx = summary.frames 下标; dropped/score/scoreValid 描述该帧
    void frameReady(int idx, int refIdx, int distIdx, bool dropped, bool scoreValid, double score,
                    const QImage& refThumb, const QImage& distThumb);
    void evalFinished(bool ok, const QString& error, double mean, double harmonic, double minS,
                      double maxS, int matched, int dropped, int frozen,
                      int markerUnrecognized, double markerCoverage, int freezeSegments,
                      int freezeMaxLen);

protected:
    void run() override;

private:
    vt::PipelineConfig cfg_;
    vt::Pipeline pipeline_;
};
