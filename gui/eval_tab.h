#pragma once

#include "../core/pipeline.h"

#include <QHash>
#include <QWidget>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
class QLabel;
class VmafChartWidget;
class EvalWorker;

class EvalTab : public QWidget {
    Q_OBJECT
public:
    explicit EvalTab(QWidget* parent = nullptr);
    ~EvalTab() override;

    // 供采集 Tab 联动: 设置目标输入 (可为截图文件夹) 并触发评估
    void setDistInput(const QString& path);
    void startEvaluation();

private slots:
    void browseDist();
    void startEval();
    void stopEval();
    void exportJson();
    void exportCsv();

    void onLog(const QString& level, const QString& msg);
    void onProgress(int done, int total);
    void onFrame(int idx, int refIdx, int distIdx, bool dropped, bool scoreValid, double score,
                 const QImage& refThumb, const QImage& distThumb);
    void onFinished(bool ok, const QString& error, double mean, double harmonic, double minS,
                    double maxS, int matched, int dropped, int frozen, int markerUnrecognized,
                    double markerCoverage, int freezeSegments, int freezeMaxLen);
    void onChartFrameSelected(int idx);

private:
    void appendLog(const QString& level, const QString& msg);
    void setRunning(bool running);

    QLineEdit* distEdit_;
    QLabel* refInfoLabel_;
    QString refPath_; // 参考片源 (自动解析, 见 ref_source.h)
    QComboBox* modelCombo_;
    QLineEdit* modelPathEdit_;
    QSpinBox* threadsSpin_;
    QLineEdit* yuvSizeEdit_;

    QPushButton* startBtn_;
    QPushButton* stopBtn_;
    QProgressBar* progress_;
    VmafChartWidget* chart_;
    QPlainTextEdit* log_;
    QLabel* resultLabel_;
    QPushButton* exportJsonBtn_;
    QPushButton* exportCsvBtn_;

    EvalWorker* worker_ = nullptr;

    // 帧回看缓存 (按 chart 帧号索引)
    struct FrameEntry {
        int refIdx = -1, distIdx = -1;
        bool dropped = false;
        double score = -1;
        int chartIdx = -1; // 在图表中的样本序号, -1 = 未进图表 (丢帧)
    };
    QVector<FrameEntry> entries_;
    int sampleCount_ = 0; // 采样模式: 有效样本计数 (图表 x 轴)
    bool samplingMode_ = false;
    vt::Summary lastSummary_;
    bool hasSummary_ = false;
};
