#pragma once

#include "../core/pipeline.h"

#include "worker.h" // EvalFrameEntry

#include <QHash>
#include <QWidget>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
class QLabel;
class QRadioButton;
class VmafChartWidget;

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
    void onFinished(bool ok, const QString& error, double mean, double harmonic, double minS,
                    double maxS, double psnrMean, int matched, int dropped, int frozen,
                    int markerUnrecognized, double markerCoverage, int freezeSegments,
                    int freezeMaxLen, const QVector<EvalFrameEntry>& frames);
    void onChartFrameSelected(int idx);
    void rebuildChart(); // VMAF/PSNR 切换时从 finalFrames_ 重画

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
    QRadioButton* vmafRadio_;
    QRadioButton* psnrRadio_;
    QPlainTextEdit* log_;
    QLabel* resultLabel_;
    QPushButton* exportJsonBtn_;
    QPushButton* exportCsvBtn_;

    EvalWorker* worker_ = nullptr;

    QVector<EvalFrameEntry> finalFrames_; // 评测完成时的全量逐帧结果 (含 flush 补齐分数), 供导出/图表
    QVector<int> chartToFinal_;         // 图表样本序号 -> finalFrames_ 下标 (点击回查)
    bool samplingMode_ = false;
    vt::Summary lastSummary_;
    bool hasSummary_ = false;
};
