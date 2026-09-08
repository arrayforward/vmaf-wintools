#include "eval_tab.h"

#include "ref_source.h"
#include "vmaf_chart_widget.h"
#include "worker.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTextStream>
#include <QVBoxLayout>

EvalTab::EvalTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    // ---- 文件选择 ----
    // 参考片源固定 (与 APK 内置片源同一份), 自动解析, 只读显示
    auto* fileForm = new QFormLayout;
    refPath_ = resolveRefSource();
    refInfoLabel_ = new QLabel;
    if (!refPath_.isEmpty()) {
        refInfoLabel_->setText(tr("参考片源: %1 (1080p60)").arg(kRefSourceName));
        refInfoLabel_->setToolTip(refPath_);
    } else {
        refInfoLabel_->setText(tr("参考片源: 未找到 %1").arg(kRefSourceName));
        refInfoLabel_->setStyleSheet("color:#ff5555;");
    }
    fileForm->addRow(tr("参考:"), refInfoLabel_);

    auto* distRow = new QHBoxLayout;
    distEdit_ = new QLineEdit;
    auto* distBtn = new QPushButton(tr("浏览…"));
    distRow->addWidget(distEdit_);
    distRow->addWidget(distBtn);
    fileForm->addRow(tr("目标视频:"), distRow);
    layout->addLayout(fileForm);

    // ---- 参数区 (对齐与裁剪全自动: QR 帧号对齐 + QR 反推视频区域) ----
    auto* paramRow = new QHBoxLayout;
    modelCombo_ = new QComboBox;
    {
        // 扫描 exe 同级 models/ 目录下的模型文件
        const QString modelsDir = QCoreApplication::applicationDirPath() + "/models";
        const QDir dir(modelsDir);
        for (const QString& f : dir.entryList({"*.json"}, QDir::Files, QDir::Name))
            modelCombo_->addItem(f, dir.absoluteFilePath(f));
    }
    modelCombo_->addItem("vmaf_v0.6.1 (内置)", "vmaf_v0.6.1");
    modelCombo_->addItem("vmaf_v0.6.1neg (内置)", "vmaf_v0.6.1neg");
    modelCombo_->addItem("vmaf_4k_v0.6.1 (内置)", "vmaf_4k_v0.6.1");
    modelCombo_->addItem(tr("自定义 JSON…"), "__custom");
    modelPathEdit_ = new QLineEdit;
    modelPathEdit_->setVisible(false);
    paramRow->addWidget(new QLabel(tr("模型:")));
    paramRow->addWidget(modelCombo_);
    paramRow->addWidget(modelPathEdit_);

    threadsSpin_ = new QSpinBox;
    threadsSpin_->setRange(0, 64);
    threadsSpin_->setValue(0);
    threadsSpin_->setSpecialValueText(tr("自动"));
    paramRow->addWidget(new QLabel(tr("线程:")));
    paramRow->addWidget(threadsSpin_);

    yuvSizeEdit_ = new QLineEdit("1920x1080");
    yuvSizeEdit_->setMaximumWidth(90);
    yuvSizeEdit_->setToolTip(tr("仅 .yuv 原始文件需要"));
    paramRow->addWidget(new QLabel(tr("YUV尺寸:")));
    paramRow->addWidget(yuvSizeEdit_);
    paramRow->addStretch();
    layout->addLayout(paramRow);

    // ---- 控制区 ----
    auto* ctrlRow = new QHBoxLayout;
    startBtn_ = new QPushButton(tr("开始评估"));
    stopBtn_ = new QPushButton(tr("停止"));
    stopBtn_->setEnabled(false);
    progress_ = new QProgressBar;
    ctrlRow->addWidget(startBtn_);
    ctrlRow->addWidget(stopBtn_);
    ctrlRow->addWidget(progress_, 1);
    layout->addLayout(ctrlRow);

    // ---- 曲线 ----
    chart_ = new VmafChartWidget;
    layout->addWidget(chart_, 1);

    // ---- 结果 + 导出 ----
    auto* resultRow = new QHBoxLayout;
    resultLabel_ = new QLabel(tr("尚未评估"));
    exportJsonBtn_ = new QPushButton(tr("导出 JSON"));
    exportCsvBtn_ = new QPushButton(tr("导出 CSV"));
    exportJsonBtn_->setEnabled(false);
    exportCsvBtn_->setEnabled(false);
    resultRow->addWidget(resultLabel_, 1);
    resultRow->addWidget(exportJsonBtn_);
    resultRow->addWidget(exportCsvBtn_);
    layout->addLayout(resultRow);

    // ---- 日志 ----
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    layout->addWidget(log_, 1);

    // ---- 信号 ----
    connect(distBtn, &QPushButton::clicked, this, &EvalTab::browseDist);
    connect(startBtn_, &QPushButton::clicked, this, &EvalTab::startEval);
    connect(stopBtn_, &QPushButton::clicked, this, &EvalTab::stopEval);
    connect(exportJsonBtn_, &QPushButton::clicked, this, &EvalTab::exportJson);
    connect(exportCsvBtn_, &QPushButton::clicked, this, &EvalTab::exportCsv);
    connect(chart_, &VmafChartWidget::frameSelected, this, &EvalTab::onChartFrameSelected);

    connect(modelCombo_, &QComboBox::currentIndexChanged, this, [this] {
        const bool custom = modelCombo_->currentData().toString() == "__custom";
        modelPathEdit_->setVisible(custom);
        if (custom) {
            const QString f = QFileDialog::getOpenFileName(this, tr("选择模型"), QString(),
                                                           "VMAF 模型 (*.json)");
            if (!f.isEmpty()) modelPathEdit_->setText(f);
        }
    });

    QSettings st("vmaf_tool");
    distEdit_->setText(st.value("lastDist").toString());
}

EvalTab::~EvalTab() {
    if (worker_) {
        worker_->requestStop();
        worker_->wait(5000);
    }
}

void EvalTab::browseDist() {
    const QString f = QFileDialog::getOpenFileName(
        this, tr("选择目标视频"), QString(), tr("视频 (*.mp4 *.mkv *.avi *.mov *.flv *.yuv);;所有文件 (*)"));
    if (f.isEmpty()) return;
    distEdit_->setText(f);
}

void EvalTab::setDistInput(const QString& path) { distEdit_->setText(path); }

void EvalTab::startEvaluation() {
    if (worker_) return; // 正在评估中
    startEval();
}

void EvalTab::appendLog(const QString& level, const QString& msg) {
    const QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString color = "#ccc";
    if (level == "warn") color = "#e6b800";
    else if (level == "error") color = "#ff5555";
    log_->appendHtml(QString("<span style='color:#888'>[%1]</span> <span style='color:%2'>%3</span>")
                         .arg(ts, color, msg.toHtmlEscaped()));
}

void EvalTab::setRunning(bool running) {
    startBtn_->setEnabled(!running);
    stopBtn_->setEnabled(running);
    exportJsonBtn_->setEnabled(!running && hasSummary_);
    exportCsvBtn_->setEnabled(!running && hasSummary_);
}

void EvalTab::startEval() {
    if (refPath_.isEmpty()) {
        QMessageBox::warning(this, tr("提示"),
                             tr("未找到参考片源 %1, 请将其放在程序目录").arg(kRefSourceName));
        return;
    }
    if (distEdit_->text().isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择目标视频或截图文件夹"));
        return;
    }

    vt::PipelineConfig cfg;
    cfg.refPath = refPath_.toStdString();
    cfg.distPath = distEdit_->text().toStdString();
    cfg.yuvRefSize = cfg.yuvDistSize = yuvSizeEdit_->text().toStdString();
    cfg.vmaf.modelName = modelCombo_->currentData().toString() == "__custom"
                             ? modelPathEdit_->text().toStdString()
                             : modelCombo_->currentData().toString().toStdString();
    cfg.vmaf.threads = threadsSpin_->value();

    // 采样模式 (目标为截图文件夹): 图表 x 轴 = 有效样本序号; 视频模式 = 参考帧号
    samplingMode_ = QFileInfo(distEdit_->text()).isDir();
    chart_->setXAxisTitle(samplingMode_ ? tr("有效样本") : tr("帧号"));

    QSettings st("vmaf_tool");
    st.setValue("lastDist", distEdit_->text());

    chart_->clear();
    entries_.clear();
    sampleCount_ = 0;
    hasSummary_ = false;
    resultLabel_->setText(tr("评估中…"));
    appendLog("info", tr("开始评估"));

    worker_ = new EvalWorker(this);
    worker_->configure(cfg);
    connect(worker_, &EvalWorker::logMessage, this, &EvalTab::onLog);
    connect(worker_, &EvalWorker::progressChanged, this, &EvalTab::onProgress);
    connect(worker_, &EvalWorker::frameReady, this, &EvalTab::onFrame);
    connect(worker_, &EvalWorker::evalFinished, this, &EvalTab::onFinished);
    setRunning(true);
    worker_->start();
}

void EvalTab::stopEval() {
    if (worker_) worker_->requestStop();
    appendLog("warn", tr("正在停止…"));
}

void EvalTab::onLog(const QString& level, const QString& msg) { appendLog(level, msg); }

void EvalTab::onProgress(int done, int total) {
    progress_->setMaximum(qMax(1, total));
    progress_->setValue(done);
    progress_->setFormat(tr("%1/%2 帧 (%p%)").arg(done).arg(total));
}

void EvalTab::onFrame(int /*idx*/, int refIdx, int distIdx, bool dropped, bool scoreValid,
                      double score, const QImage& /*refThumb*/, const QImage& /*distThumb*/) {
    // 以参考帧号为 key 更新/新增条目
    int pos = -1;
    for (int i = entries_.size() - 1; i >= 0 && i > entries_.size() - 6; --i) {
        if (entries_[i].refIdx == refIdx) {
            pos = i;
            break;
        }
    }
    if (pos < 0) {
        FrameEntry e;
        e.refIdx = refIdx;
        e.distIdx = distIdx;
        e.dropped = dropped;
        entries_.append(e);
        pos = entries_.size() - 1;
        // 丢帧不进图表; 有效样本以 -1 占位进图表 (分数延迟一帧回填)
        if (!dropped) {
            const int frameNo = samplingMode_ ? ++sampleCount_ : refIdx;
            entries_[pos].chartIdx = chart_->frameCount();
            chart_->appendFrame(-1, frameNo);
        }
    }
    auto& e = entries_[pos];
    e.dropped = dropped;
    e.distIdx = distIdx;
    if (scoreValid) {
        e.score = score;
        if (e.chartIdx >= 0) chart_->updateFrame(e.chartIdx, score);
        appendLog("info", tr("帧 %1 VMAF=%2").arg(refIdx).arg(score, 0, 'f', 2));
    }
}

void EvalTab::onFinished(bool ok, const QString& error, double mean, double harmonic, double minS,
                         double maxS, int matched, int dropped, int frozen,
                         int markerUnrecognized, double markerCoverage, int freezeSegments,
                         int freezeMaxLen) {
    setRunning(false);
    if (ok) {
        hasSummary_ = true;
        const double dropRate = matched + dropped > 0
                                    ? 100.0 * dropped / (matched + dropped)
                                    : 0.0;
        QString text = tr("VMAF mean=%1  harmonic=%2  min=%3  max=%4  |  匹配 %5 帧  丢帧 %6 (%7%)  冻结帧 %8")
                           .arg(mean, 0, 'f', 2)
                           .arg(harmonic, 0, 'f', 2)
                           .arg(minS, 0, 'f', 2)
                           .arg(maxS, 0, 'f', 2)
                           .arg(matched)
                           .arg(dropped)
                           .arg(dropRate, 0, 'f', 1)
                           .arg(frozen);
        // 帧号标记统计 (片源烧录了帧号时才有意义)
        if (markerCoverage > 0 || markerUnrecognized > 0) {
            text += tr("  |  帧号覆盖率 %1%  未识别 %2  冻结段 %3  最长冻结 %4")
                        .arg(markerCoverage * 100, 0, 'f', 1)
                        .arg(markerUnrecognized)
                        .arg(freezeSegments)
                        .arg(freezeMaxLen);
        }
        resultLabel_->setText(text);
        exportJsonBtn_->setEnabled(true);
        exportCsvBtn_->setEnabled(true);
    } else {
        resultLabel_->setText(tr("评估失败: %1").arg(error));
        appendLog("error", error);
    }
    if (worker_) {
        worker_->wait(5000);
        worker_->deleteLater();
        worker_ = nullptr;
    }
}

void EvalTab::onChartFrameSelected(int idx) {
    // idx 为图表样本序号, 找到对应条目
    for (const auto& e : entries_) {
        if (e.chartIdx != idx) continue;
        appendLog("info", tr("查看帧 %1 → 目标帧 %2, VMAF=%3")
                              .arg(e.refIdx)
                              .arg(e.distIdx)
                              .arg(e.score, 0, 'f', 2));
        return;
    }
}

void EvalTab::exportJson() {
    const QString f = QFileDialog::getSaveFileName(this, tr("导出 JSON"), "vmaf_result.json",
                                                   "JSON (*.json)");
    if (f.isEmpty()) return;
    QJsonArray frames;
    for (const auto& e : entries_) {
        QJsonObject o;
        o["ref_frame"] = e.refIdx;
        o["dist_frame"] = e.distIdx;
        o["dropped"] = e.dropped;
        if (!e.dropped && e.score >= 0) o["vmaf"] = e.score;
        frames.append(o);
    }
    QJsonObject root;
    root["reference"] = refPath_;
    root["distorted"] = distEdit_->text();
    root["frames"] = frames;
    QFile file(f);
    if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(root).toJson());
}

void EvalTab::exportCsv() {
    const QString f = QFileDialog::getSaveFileName(this, tr("导出 CSV"), "vmaf_result.csv",
                                                   "CSV (*.csv)");
    if (f.isEmpty()) return;
    QFile file(f);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream ts(&file);
    ts << "ref_frame,dist_frame,status,vmaf\n";
    for (const auto& e : entries_) {
        ts << e.refIdx << ',' << e.distIdx << ',' << (e.dropped ? "dropped" : "matched") << ',';
        if (!e.dropped && e.score >= 0) ts << QString::number(e.score, 'f', 4);
        ts << '\n';
    }
}
