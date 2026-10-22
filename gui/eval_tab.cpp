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
#include <QRadioButton>
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

    // ---- 曲线 (VMAF / PSNR 切换) ----
    auto* chartRow = new QHBoxLayout;
    vmafRadio_ = new QRadioButton("VMAF");
    psnrRadio_ = new QRadioButton("PSNR");
    vmafRadio_->setChecked(true);
    chartRow->addWidget(new QLabel(tr("曲线:")));
    chartRow->addWidget(vmafRadio_);
    chartRow->addWidget(psnrRadio_);
    chartRow->addStretch();
    layout->addLayout(chartRow);
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
    connect(vmafRadio_, &QRadioButton::toggled, this, &EvalTab::rebuildChart);
    connect(psnrRadio_, &QRadioButton::toggled, this, &EvalTab::rebuildChart);

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
        // 先请求停止再等待线程退出, 确保 QThread 不会在运行中被销毁 (qFatal)
        worker_->requestStop();
        worker_->wait();
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
    if (worker_) return; // 正在评估中 (含收尾阶段)
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
    if (worker_) return; // 重入保护: 上一轮 worker 尚未销毁
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
    chartToFinal_.clear();
    hasSummary_ = false;
    resultLabel_->setText(tr("评估中…"));
    appendLog("info", tr("开始评估"));

    worker_ = new EvalWorker(this);
    worker_->configure(cfg);
    connect(worker_, &EvalWorker::logMessage, this, &EvalTab::onLog);
    connect(worker_, &EvalWorker::progressChanged, this, &EvalTab::onProgress);
    connect(worker_, &EvalWorker::evalFinished, this, &EvalTab::onFinished);
    // 标准 QThread 生命周期: 线程退出后自动销毁, 不在 onFinished 里 wait/deleteLater
    connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
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

void EvalTab::onFinished(bool ok, const QString& error, double mean, double harmonic, double minS,
                         double maxS, double psnrMean, int matched, int dropped, int frozen,
                         int markerUnrecognized, double markerCoverage, int freezeSegments,
                         int freezeMaxLen, const QVector<EvalFrameEntry>& frames) {
    setRunning(false);
    if (ok) {
        hasSummary_ = true;
        finalFrames_ = frames;
        // 供导出 summary 段使用
        lastSummary_.mean = mean;
        lastSummary_.harmonicMean = harmonic;
        lastSummary_.minScore = minS;
        lastSummary_.maxScore = maxS;
        lastSummary_.psnrMean = psnrMean;
        lastSummary_.matched = matched;
        lastSummary_.dropped = dropped;
        lastSummary_.frozen = frozen;
        lastSummary_.markerUnrecognized = markerUnrecognized;
        lastSummary_.markerCoverage = markerCoverage;
        lastSummary_.freezeSegments = freezeSegments;
        lastSummary_.freezeMaxLen = freezeMaxLen;
        const double dropRate = matched + dropped > 0
                                    ? 100.0 * dropped / (matched + dropped)
                                    : 0.0;
        QString text = tr("VMAF mean=%1  harmonic=%2  min=%3  max=%4  PSNR avg mean=%5 dB  |  匹配 %6 帧  丢帧 %7 (%8%)  冻结帧 %9")
                           .arg(mean, 0, 'f', 2)
                           .arg(harmonic, 0, 'f', 2)
                           .arg(minS, 0, 'f', 2)
                           .arg(maxS, 0, 'f', 2)
                           .arg(psnrMean, 0, 'f', 2)
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
        rebuildChart();

        exportJsonBtn_->setEnabled(true);
        exportCsvBtn_->setEnabled(true);
    } else {
        resultLabel_->setText(tr("评估失败: %1").arg(error));
        appendLog("error", error);
    }
    // worker 由 finished → deleteLater 自动销毁 (worker_ 为 QPointer, 销毁后自动置空);
    // 这里只消费结果数据 (finalFrames_ 为值拷贝, 与 worker 生命周期无关)
}

void EvalTab::rebuildChart() {
    // 图表从全量结果重建 (该 libvmaf 构建 flush 前不可取分, 过程中没有分数可画):
    // 只画指标有效的样本, 折线连接相邻样本; x 轴: 采样模式样本序号 / 视频模式帧号
    const bool showPsnr = psnrRadio_->isChecked();
    chart_->setYAxisTitle(showPsnr ? "dB" : "VMAF");
    chart_->clear();
    chartToFinal_.clear();
    int sampleNo = 0;
    for (int i = 0; i < finalFrames_.size(); ++i) {
        const auto& e = finalFrames_[i];
        const double v = showPsnr ? e.psnr : (e.scoreValid ? e.score : -1.0);
        if (e.dropped || v < 0) continue;
        chartToFinal_.append(i);
        chart_->appendFrame(v, samplingMode_ ? ++sampleNo : e.refIdx);
    }
}

void EvalTab::onChartFrameSelected(int idx) {
    // idx 为图表样本序号, 经 chartToFinal_ 反查 finalFrames_
    if (idx < 0 || idx >= chartToFinal_.size()) return;
    const auto& e = finalFrames_[chartToFinal_[idx]];
    appendLog("info", tr("查看帧 %1 → 目标帧 %2, VMAF=%3, PSNR avg=%4 dB (Y/U/V=%5/%6/%7)")
                          .arg(e.refIdx)
                          .arg(e.distIdx)
                          .arg(e.score, 0, 'f', 2)
                          .arg(e.psnr, 0, 'f', 2)
                          .arg(e.psnrY, 0, 'f', 2)
                          .arg(e.psnrU, 0, 'f', 2)
                          .arg(e.psnrV, 0, 'f', 2));
}

void EvalTab::exportJson() {
    const QString f = QFileDialog::getSaveFileName(this, tr("导出 JSON"), "vmaf_result.json",
                                                   "JSON (*.json)");
    if (f.isEmpty()) return;
    // 数据源是评测完成时的 Summary.frames 全量 (含 flush 后补齐的分数),
    // 采样模式下只含匹配上的样本
    QJsonArray frames;
    for (const auto& e : finalFrames_) {
        QJsonObject o;
        o["ref_frame"] = e.refIdx;
        o["status"] = e.dropped ? "dropped" : "matched";
        if (!e.dropped) {
            o["dist_frame"] = e.distIdx;
            if (e.scoreValid) o["vmaf"] = e.score;
            if (e.psnr >= 0) { // psnr 为 Y/U/V 加权平均口径
                o["psnr"] = e.psnr;
                o["psnr_y"] = e.psnrY;
                o["psnr_u"] = e.psnrU;
                o["psnr_v"] = e.psnrV;
            }
        }
        frames.append(o);
    }
    QJsonObject sum;
    sum["mean"] = lastSummary_.mean;
    sum["harmonic_mean"] = lastSummary_.harmonicMean;
    sum["min"] = lastSummary_.minScore;
    sum["max"] = lastSummary_.maxScore;
    sum["psnr_mean"] = lastSummary_.psnrMean;
    sum["matched"] = lastSummary_.matched;
    sum["dropped"] = lastSummary_.dropped;
    sum["frozen"] = lastSummary_.frozen;
    sum["freeze_segments"] = lastSummary_.freezeSegments;
    sum["freeze_max_len"] = lastSummary_.freezeMaxLen;
    sum["marker_unrecognized"] = lastSummary_.markerUnrecognized;
    sum["marker_coverage"] = lastSummary_.markerCoverage;
    QJsonObject root;
    root["reference"] = refPath_;
    root["distorted"] = distEdit_->text();
    root["summary"] = sum;
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
    ts << "ref_frame,dist_frame,vmaf,psnr_avg,psnr_y,psnr_u,psnr_v,status\n";
    for (const auto& e : finalFrames_) {
        ts << e.refIdx << ',';
        if (!e.dropped) ts << e.distIdx;
        ts << ',';
        if (!e.dropped && e.scoreValid) ts << QString::number(e.score, 'f', 4);
        ts << ',';
        if (!e.dropped && e.psnr >= 0) {
            ts << QString::number(e.psnr, 'f', 4) << ',' << QString::number(e.psnrY, 'f', 4) << ','
               << QString::number(e.psnrU, 'f', 4) << ',' << QString::number(e.psnrV, 'f', 4);
        } else {
            ts << ",,,";
        }
        ts << ',' << (e.dropped ? "dropped" : "matched") << '\n';
    }
}
