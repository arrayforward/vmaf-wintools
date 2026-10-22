#include "jank_tab.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

JankTab::JankTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    // ---- 输入 ----
    auto* form = new QFormLayout;
    auto* inRow = new QHBoxLayout;
    inputEdit_ = new QLineEdit;
    inputEdit_->setPlaceholderText(tr("录屏视频 (APK 播放带 QR 帧号片源的画面)"));
    auto* inBtn = new QPushButton(tr("浏览…"));
    inRow->addWidget(inputEdit_);
    inRow->addWidget(inBtn);
    form->addRow(tr("录屏视频:"), inRow);
    layout->addLayout(form);

    // ---- 控制 ----
    auto* ctrlRow = new QHBoxLayout;
    startBtn_ = new QPushButton(tr("开始分析"));
    stopBtn_ = new QPushButton(tr("停止"));
    stopBtn_->setEnabled(false);
    progress_ = new QProgressBar;
    ctrlRow->addWidget(startBtn_);
    ctrlRow->addWidget(stopBtn_);
    ctrlRow->addWidget(progress_, 1);
    layout->addLayout(ctrlRow);

    // ---- 结果 + 导出 ----
    auto* resultRow = new QHBoxLayout;
    resultLabel_ = new QLabel(tr("尚未分析"));
    resultLabel_->setWordWrap(true);
    exportCsvBtn_ = new QPushButton(tr("导出 CSV"));
    exportCsvBtn_->setEnabled(false);
    resultRow->addWidget(resultLabel_, 1);
    resultRow->addWidget(exportCsvBtn_);
    layout->addLayout(resultRow);

    // ---- 卡顿事件列表 ----
    eventTable_ = new QTableWidget(0, 4);
    eventTable_->setHorizontalHeaderLabels({tr("时刻 (ms)"), tr("源帧号"), tr("显示耗时 (ms)"),
                                            tr("严重")});
    eventTable_->horizontalHeader()->setStretchLastSection(true);
    eventTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    eventTable_->setSelectionMode(QAbstractItemView::NoSelection);
    eventTable_->verticalHeader()->setVisible(false);
    layout->addWidget(eventTable_, 1);

    // ---- 日志 ----
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    log_->setMaximumHeight(140);
    layout->addWidget(log_);

    // ---- 信号 ----
    connect(inBtn, &QPushButton::clicked, this, &JankTab::browseInput);
    connect(startBtn_, &QPushButton::clicked, this, &JankTab::startAnalysis);
    connect(stopBtn_, &QPushButton::clicked, this, &JankTab::stopAnalysis);
    connect(exportCsvBtn_, &QPushButton::clicked, this, &JankTab::exportCsv);

    QSettings st("vmaf_tool");
    inputEdit_->setText(st.value("lastJankInput").toString());
}

JankTab::~JankTab() {
    if (worker_) {
        // 先请求停止再等待线程退出, 确保 QThread 不会在运行中被销毁 (qFatal)
        worker_->requestStop();
        worker_->wait();
    }
}

void JankTab::browseInput() {
    const QString f = QFileDialog::getOpenFileName(
        this, tr("选择录屏视频"), QString(), tr("视频 (*.mp4 *.mkv *.avi *.mov *.flv);;所有文件 (*)"));
    if (f.isEmpty()) return;
    inputEdit_->setText(f);
}

void JankTab::appendLog(const QString& level, const QString& msg) {
    const QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString color = "#ccc";
    if (level == "warn") color = "#e6b800";
    else if (level == "error") color = "#ff5555";
    log_->appendHtml(QString("<span style='color:#888'>[%1]</span> <span style='color:%2'>%3</span>")
                         .arg(ts, color, msg.toHtmlEscaped()));
}

void JankTab::setRunning(bool running) {
    startBtn_->setEnabled(!running);
    stopBtn_->setEnabled(running);
    exportCsvBtn_->setEnabled(!running && hasResult_);
}

void JankTab::startAnalysis() {
    if (worker_) return; // 重入保护: 上一轮 worker 尚未销毁
    if (inputEdit_->text().isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择录屏视频"));
        return;
    }

    QSettings st("vmaf_tool");
    st.setValue("lastJankInput", inputEdit_->text());

    segs_.clear();
    hasResult_ = false;
    eventTable_->setRowCount(0);
    progress_->setValue(0);
    resultLabel_->setText(tr("分析中…"));
    appendLog("info", tr("开始分析"));

    worker_ = new JankWorker(this);
    worker_->configure(inputEdit_->text());
    connect(worker_, &JankWorker::logMessage, this, &JankTab::onLog);
    connect(worker_, &JankWorker::progressChanged, this, &JankTab::onProgress);
    connect(worker_, &JankWorker::jankFinished, this, &JankTab::onFinished);
    // 标准 QThread 生命周期: 线程退出后自动销毁
    connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
    setRunning(true);
    worker_->start();
}

void JankTab::stopAnalysis() {
    if (worker_) worker_->requestStop();
    appendLog("warn", tr("正在停止…"));
}

void JankTab::onLog(const QString& level, const QString& msg) { appendLog(level, msg); }

void JankTab::onProgress(int done, int total) {
    if (total > 0) progress_->setMaximum(total);
    progress_->setValue(done);
    progress_->setFormat(total > 0 ? tr("%1/%2 帧 (%p%)").arg(done).arg(total)
                                   : tr("已解码 %1 帧").arg(done));
}

void JankTab::onFinished(bool ok, const QString& error, double captureFps, double durationMs,
                         int totalFrames, int unrecognized, double avgFrameMs, double maxFrameMs,
                         int jankCount, int bigJankCount, double jankRate, double bigJankRate,
                         double jankPer10Min, const QVector<JankSegEntry>& segs) {
    setRunning(false);
    if (ok) {
        hasResult_ = true;
        segs_ = segs;
        resultLabel_->setText(
            tr("录屏 %1fps / %2 秒 / 有效帧 %3 | 平均帧耗时 %4ms 最大 %5ms | "
               "Jank %6 次 / Big Jank %7 次 | 卡顿率 %8% 严重卡顿率 %9% | Jank/10min %10 | "
               "未识别 %11 帧")
                .arg(captureFps, 0, 'f', 2)
                .arg(durationMs / 1000.0, 0, 'f', 1)
                .arg(totalFrames)
                .arg(avgFrameMs, 0, 'f', 2)
                .arg(maxFrameMs, 0, 'f', 2)
                .arg(jankCount)
                .arg(bigJankCount)
                .arg(jankRate * 100, 0, 'f', 2)
                .arg(bigJankRate * 100, 0, 'f', 2)
                .arg(jankPer10Min, 0, 'f', 1)
                .arg(unrecognized));
        // 卡顿事件列表
        int row = 0;
        for (const auto& s : segs_) {
            if (!s.jank) continue;
            eventTable_->insertRow(row);
            eventTable_->setItem(row, 0, new QTableWidgetItem(QString::number(s.startMs)));
            eventTable_->setItem(row, 1, new QTableWidgetItem(QString::number(s.frameIndex)));
            eventTable_->setItem(row, 2,
                                 new QTableWidgetItem(QString::number(s.displayMs, 'f', 1)));
            auto* bigItem = new QTableWidgetItem(s.bigJank ? tr("Big Jank") : tr("Jank"));
            if (s.bigJank) bigItem->setForeground(QColor(255, 85, 85));
            eventTable_->setItem(row, 3, bigItem);
            ++row;
        }
        exportCsvBtn_->setEnabled(true);
        appendLog("info", tr("分析完成: Jank %1 次, Big Jank %2 次").arg(jankCount).arg(bigJankCount));
    } else {
        resultLabel_->setText(tr("分析失败: %1").arg(error));
        appendLog("error", error);
    }
    // worker 由 finished → deleteLater 自动销毁, 这里只消费结果数据
}

void JankTab::exportCsv() {
    const QString f = QFileDialog::getSaveFileName(this, tr("导出 CSV"), "jank_result.csv",
                                                   "CSV (*.csv)");
    if (f.isEmpty()) return;
    QFile file(f);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    // 与 jank_cli 相同格式: 每个 distinct 源帧一行
    QTextStream ts(&file);
    ts << "seq,frameIndex,displayMs,jank,bigJank\n";
    for (int i = 0; i < segs_.size(); ++i) {
        const auto& s = segs_[i];
        ts << i << ',' << s.frameIndex << ',' << QString::number(s.displayMs, 'f', 2) << ','
           << (s.jank ? 1 : 0) << ',' << (s.bigJank ? 1 : 0) << '\n';
    }
    appendLog("info", tr("CSV 已导出: %1 (%2 行)").arg(f).arg(segs_.size()));
}
