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
    double psnr = -1.0; // -1 = 无效
};
Q_DECLARE_METATYPE(EvalFrameEntry)

// 卡顿分析: 逐显示段结果 (与 vt::JankSegment 对应)
struct JankSegEntry {
    int frameIndex = -1;
    double displayMs = 0;
    qint64 startMs = 0;
    bool jank = false, bigJank = false;
};
Q_DECLARE_METATYPE(JankSegEntry)

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
                      double maxS, double psnrMean, int matched, int dropped, int frozen,
                      int markerUnrecognized, double markerCoverage, int freezeSegments,
                      int freezeMaxLen, const QVector<EvalFrameEntry>& frames);

protected:
    void run() override;

private:
    vt::PipelineConfig cfg_;
    vt::Pipeline pipeline_;
};

// 卡顿分析 worker: 逐帧 decodeMarker 收集帧号序列, 结束后 analyzeJank
class JankWorker : public QThread {
    Q_OBJECT
public:
    explicit JankWorker(QObject* parent = nullptr);

    void configure(const QString& path);
    void requestStop();

signals:
    void logMessage(const QString& level, const QString& msg);
    void progressChanged(int done, int total);
    // segs = JankReport.segments 全量 (逐显示段), 供事件列表与导出
    void jankFinished(bool ok, const QString& error, double captureFps, double durationMs,
                      int totalFrames, int unrecognized, double avgFrameMs, double maxFrameMs,
                      int jankCount, int bigJankCount, double jankRate, double bigJankRate,
                      double jankPer10Min, const QVector<JankSegEntry>& segs);

protected:
    void run() override;

private:
    std::string path_;
    std::atomic<bool> stopRequested_{false};
};
