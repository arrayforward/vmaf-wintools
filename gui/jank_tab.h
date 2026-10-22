#pragma once

#include "worker.h" // JankWorker / JankSegEntry

#include <QPointer>
#include <QWidget>

class QLineEdit;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
class QLabel;
class QTableWidget;

// 卡顿分析: 录屏 (APK 播放带 QR 帧号片源的画面) 逐帧识别帧号, PerfDog 标准判定卡顿
class JankTab : public QWidget {
    Q_OBJECT
public:
    explicit JankTab(QWidget* parent = nullptr);
    ~JankTab() override;

private slots:
    void browseInput();
    void startAnalysis();
    void stopAnalysis();
    void exportCsv();

    void onLog(const QString& level, const QString& msg);
    void onProgress(int done, int total);
    void onFinished(bool ok, const QString& error, double captureFps, double durationMs,
                    int totalFrames, int unrecognized, double avgFrameMs, double maxFrameMs,
                    int jankCount, int bigJankCount, double jankRate, double bigJankRate,
                    double jankPer10Min, const QVector<JankSegEntry>& segs);

private:
    void appendLog(const QString& level, const QString& msg);
    void setRunning(bool running);

    QLineEdit* inputEdit_;
    QPushButton* startBtn_;
    QPushButton* stopBtn_;
    QProgressBar* progress_;
    QLabel* resultLabel_;
    QTableWidget* eventTable_;
    QPushButton* exportCsvBtn_;
    QPlainTextEdit* log_;

    QPointer<JankWorker> worker_; // finished → deleteLater 自动销毁, 销毁后自动置空
    QVector<JankSegEntry> segs_; // 逐显示段全量结果, 供导出
    bool hasResult_ = false;
};