#pragma once

#include "../core/pipeline.h"

#include <QHash>
#include <QWidget>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
class QLabel;
class CropSelectWidget;
class VmafChartWidget;
class EvalWorker;

class EvalTab : public QWidget {
    Q_OBJECT
public:
    explicit EvalTab(QWidget* parent = nullptr);
    ~EvalTab() override;

private slots:
    void browseRef();
    void browseDist();
    void loadFirstFrames();
    void startEval();
    void stopEval();
    void exportJson();
    void exportCsv();

    void onLog(const QString& level, const QString& msg);
    void onProgress(int done, int total);
    void onFrame(int idx, int refIdx, int distIdx, bool dropped, bool scoreValid, double score,
                 const QImage& refThumb, const QImage& distThumb);
    void onFinished(bool ok, const QString& error, double mean, double harmonic, double minS,
                    double maxS, int matched, int dropped, int frozen);
    void onChartFrameSelected(int idx);
    void onCropEdited(const QRect& rect);

private:
    void appendLog(const QString& level, const QString& msg);
    void setRunning(bool running);

    QLineEdit* refEdit_;
    QLineEdit* distEdit_;
    QComboBox* modelCombo_;
    QLineEdit* modelPathEdit_;
    QComboBox* alignCombo_;
    QSpinBox* threadsSpin_;
    QLineEdit* yuvSizeEdit_;

    CropSelectWidget* refCropView_;
    CropSelectWidget* distCropView_;
    QCheckBox* linkCropCheck_;
    QSpinBox *cropX_, *cropY_, *cropW_, *cropH_;
    bool updatingSpinboxes_ = false;

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
        QImage refThumb, distThumb;
    };
    QVector<FrameEntry> entries_;
    vt::Summary lastSummary_;
    bool hasSummary_ = false;
};
