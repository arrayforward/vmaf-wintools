#include "capture_tab.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

CaptureTab::CaptureTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    // ---- adb / 设备 ----
    auto* devGroup = new QGroupBox(tr("设备"));
    auto* devForm = new QFormLayout(devGroup);

    auto* adbRow = new QHBoxLayout;
    adbEdit_ = new QLineEdit("adb");
    auto* adbBtn = new QPushButton(tr("浏览…"));
    adbRow->addWidget(adbEdit_);
    adbRow->addWidget(adbBtn);
    devForm->addRow(tr("adb 路径:"), adbRow);

    auto* devRow = new QHBoxLayout;
    deviceCombo_ = new QComboBox;
    deviceCombo_->setEditable(true);
    refreshBtn_ = new QPushButton(tr("刷新"));
    devRow->addWidget(deviceCombo_, 1);
    devRow->addWidget(refreshBtn_);
    devForm->addRow(tr("设备:"), devRow);
    layout->addWidget(devGroup);

    // ---- 参数 ----
    auto* paramRow = new QHBoxLayout;
    intervalSpin_ = new QSpinBox;
    intervalSpin_->setRange(100, 60000);
    intervalSpin_->setValue(1000);
    intervalSpin_->setSingleStep(100);
    intervalSpin_->setSuffix(" ms");
    paramRow->addWidget(new QLabel(tr("截图间隔:")));
    paramRow->addWidget(intervalSpin_);

    durationSpin_ = new QSpinBox;
    durationSpin_->setRange(1, 86400);
    durationSpin_->setValue(60);
    durationSpin_->setSuffix(tr(" 秒"));
    paramRow->addWidget(new QLabel(tr("总时长:")));
    paramRow->addWidget(durationSpin_);

    autoSendCheck_ = new QCheckBox(tr("采集完成后送评测"));
    autoSendCheck_->setChecked(true);
    paramRow->addWidget(autoSendCheck_);
    paramRow->addStretch();
    layout->addLayout(paramRow);

    auto* outRow = new QHBoxLayout;
    outDirEdit_ = new QLineEdit(defaultOutDir());
    auto* outBtn = new QPushButton(tr("浏览…"));
    outRow->addWidget(outDirEdit_, 1);
    outRow->addWidget(outBtn);
    layout->addLayout(outRow);

    // ---- 控制 ----
    auto* ctrlRow = new QHBoxLayout;
    startBtn_ = new QPushButton(tr("开始采集"));
    stopBtn_ = new QPushButton(tr("停止"));
    stopBtn_->setEnabled(false);
    sendBtn_ = new QPushButton(tr("送去评测"));
    sendBtn_->setEnabled(false);
    sendBtn_->setToolTip(tr("把最近一次采集的输出文件夹填到评测 Tab 并开始评估"));
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    ctrlRow->addWidget(startBtn_);
    ctrlRow->addWidget(stopBtn_);
    ctrlRow->addWidget(sendBtn_);
    ctrlRow->addWidget(progress_, 1);
    layout->addLayout(ctrlRow);

    statusLabel_ = new QLabel(tr("空闲"));
    layout->addWidget(statusLabel_);

    // ---- 预览 + 日志 ----
    auto* bottomRow = new QHBoxLayout;
    previewLabel_ = new QLabel(tr("尚无截图"));
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMinimumSize(320, 240);
    previewLabel_->setStyleSheet("background:#222; color:#888;");
    bottomRow->addWidget(previewLabel_, 1);
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    bottomRow->addWidget(log_, 2);
    layout->addLayout(bottomRow, 1);

    // ---- 信号 ----
    connect(adbBtn, &QPushButton::clicked, this, &CaptureTab::browseAdb);
    connect(refreshBtn_, &QPushButton::clicked, this, &CaptureTab::refreshDevices);
    connect(outBtn, &QPushButton::clicked, this, &CaptureTab::browseOutDir);
    connect(startBtn_, &QPushButton::clicked, this, &CaptureTab::startCapture);
    connect(stopBtn_, &QPushButton::clicked, this, &CaptureTab::stopCapture);
    connect(sendBtn_, &QPushButton::clicked, this, &CaptureTab::onSendClicked);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &CaptureTab::onTick);

    refreshDevices();
}

CaptureTab::~CaptureTab() {
    timer_->stop();
    if (proc_) {
        proc_->kill();
        proc_->waitForFinished(3000);
    }
    if (manifest_.isOpen()) manifest_.close();
}

QString CaptureTab::defaultOutDir() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (base.isEmpty()) base = QDir::currentPath();
    return base + "/vmaf_captures/" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
}

void CaptureTab::appendLog(const QString& level, const QString& msg) {
    const QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString color = "#ccc";
    if (level == "warn") color = "#e6b800";
    else if (level == "error") color = "#ff5555";
    log_->appendHtml(QString("<span style='color:#888'>[%1]</span> <span style='color:%2'>%3</span>")
                         .arg(ts, color, msg.toHtmlEscaped()));
}

void CaptureTab::browseAdb() {
    const QString f = QFileDialog::getOpenFileName(this, tr("选择 adb"), QString(),
                                                   "adb (adb.exe adb);;所有文件 (*)");
    if (!f.isEmpty()) adbEdit_->setText(f);
}

void CaptureTab::browseOutDir() {
    const QString d = QFileDialog::getExistingDirectory(this, tr("选择输出文件夹"),
                                                        outDirEdit_->text());
    if (!d.isEmpty()) outDirEdit_->setText(d);
}

void CaptureTab::refreshDevices() {
    const QString adb = adbEdit_->text().trimmed();
    const QString prev = deviceCombo_->currentText().trimmed();
    deviceCombo_->clear();

    QProcess p;
    p.start(adb, {"devices"});
    if (!p.waitForStarted(3000)) {
        QMessageBox::warning(this, tr("提示"),
                             tr("无法启动 adb (%1)。\n请检查 adb 路径配置, 或手动填写 adb 完整路径。")
                                 .arg(adb));
        appendLog("error", tr("无法启动 adb: %1, 请检查路径").arg(adb));
        return;
    }
    p.waitForFinished(5000);
    const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
    for (const QString& line : out.split('\n', Qt::SkipEmptyParts)) {
        const QString t = line.trimmed();
        if (t.startsWith("List of devices") || t.startsWith("*")) continue;
        const QStringList parts = t.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() >= 2 && parts[1] == "device") deviceCombo_->addItem(parts[0]);
    }
    const int idx = deviceCombo_->findText(prev);
    if (idx >= 0) deviceCombo_->setCurrentIndex(idx);
    if (deviceCombo_->count() == 0)
        appendLog("warn", tr("未发现设备 (adb devices 为空), 请确认设备已连接并授权"));
    else
        appendLog("info", tr("发现 %1 台设备").arg(deviceCombo_->count()));
}

void CaptureTab::setCapturing(bool capturing) {
    startBtn_->setEnabled(!capturing);
    stopBtn_->setEnabled(capturing);
    refreshBtn_->setEnabled(!capturing);
    sendBtn_->setEnabled(!capturing && !lastDir_.isEmpty());
}

void CaptureTab::startCapture() {
    const QString serial = deviceCombo_->currentText().trimmed();
    if (serial.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择设备 (点\"刷新\"获取设备列表)"));
        return;
    }
    QString dir = outDirEdit_->text().trimmed();
    if (dir.isEmpty()) {
        dir = defaultOutDir();
        outDirEdit_->setText(dir);
    }
    if (!QDir().mkpath(dir)) {
        appendLog("error", tr("无法创建输出文件夹: %1").arg(dir));
        return;
    }

    outDir_ = dir;
    manifest_.setFileName(dir + "/manifest.csv");
    if (!manifest_.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendLog("error", tr("无法写 manifest.csv: %1").arg(manifest_.fileName()));
        return;
    }
    manifest_.write("filename,wallclock_ms\n");

    count_ = 0;
    firstTs_ = lastTs_ = 0;
    stopping_ = false;
    elapsed_.start();
    setCapturing(true);
    progress_->setValue(0);
    appendLog("info", tr("开始采集: 设备 %1, 间隔 %2 ms, 时长 %3 秒 -> %4")
                          .arg(serial)
                          .arg(intervalSpin_->value())
                          .arg(durationSpin_->value())
                          .arg(dir));

    onTick(); // 立即截第一张
    timer_->start(intervalSpin_->value());
}

void CaptureTab::stopCapture() {
    if (!startBtn_->isEnabled()) { // 采集中
        timer_->stop();
        stopping_ = false;
        if (proc_) {
            proc_->kill();
            proc_->waitForFinished(3000); // 确保进程清理
        }
        finishCapture(true);
    }
}

void CaptureTab::onTick() {
    const int totalMs = durationSpin_->value() * 1000;
    const qint64 el = elapsed_.elapsed();
    progress_->setValue(qMin(100, static_cast<int>(el * 100 / totalMs)));
    statusLabel_->setText(tr("已截 %1 张, 剩余 %2 秒")
                              .arg(count_)
                              .arg(qMax(0, (totalMs - static_cast<int>(el)) / 1000)));

    if (el >= totalMs) {
        timer_->stop();
        if (proc_ && proc_->state() == QProcess::Running) {
            stopping_ = true; // 等最后一张截完再收尾
        } else {
            finishCapture(false);
        }
        return;
    }

    if (proc_ && proc_->state() == QProcess::Running) {
        // 设备掉线等情况 screencap 可能挂起, 超时强杀以免影响后续截图
        if (el - procStartMs_ > 10000) {
            appendLog("warn", tr("上一次截图超时, 已终止 (设备可能已断开)"));
            proc_->kill();
        } else {
            appendLog("warn", tr("上一次截图尚未完成, 跳过本次"));
        }
        return;
    }

    const QString serial = deviceCombo_->currentText().trimmed();
    procStartMs_ = el;
    proc_ = new QProcess(this);
    // exec-out 输出原始二进制 PNG, 读标准输出通道
    proc_->setProcessChannelMode(QProcess::SeparateChannels);
    proc_->setReadChannel(QProcess::StandardOutput);
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &CaptureTab::onProcFinished);
    proc_->start(adbEdit_->text().trimmed(),
                 {"-s", serial, "exec-out", "screencap", "-p"});
}

void CaptureTab::onProcFinished(int exitCode, QProcess::ExitStatus status) {
    auto* proc = qobject_cast<QProcess*>(sender());
    if (!proc) return;
    const QByteArray data = proc->readAllStandardOutput();
    const QString errOut = QString::fromLocal8Bit(proc->readAllStandardError()).trimmed();
    proc->deleteLater();
    if (proc == proc_) proc_ = nullptr;

    const bool ok = status == QProcess::NormalExit && exitCode == 0 &&
                    data.size() > 8 && data.startsWith("\x89PNG");
    if (ok) {
        const QString name =
            QString("%1.png").arg(count_ + 1, 5, 10, QChar('0'));
        QFile f(outDir_ + "/" + name);
        if (f.open(QIODevice::WriteOnly) && f.write(data) == data.size()) {
            ++count_;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (firstTs_ == 0) firstTs_ = now;
            lastTs_ = now;
            manifest_.write(QString("%1,%2\n").arg(name).arg(now).toUtf8());
            manifest_.flush();
            const QImage img = QImage::fromData(data, "PNG");
            if (!img.isNull())
                previewLabel_->setPixmap(QPixmap::fromImage(img).scaled(
                    previewLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            appendLog("error", tr("写文件失败: %1").arg(name));
        }
    } else {
        appendLog("error", tr("截图失败 (退出码 %1): %2").arg(exitCode).arg(
                      errOut.isEmpty() ? tr("无错误输出") : errOut));
    }

    if (stopping_ && !proc_) {
        stopping_ = false;
        finishCapture(false);
    }
}

void CaptureTab::finishCapture(bool cancelled) {
    timer_->stop();
    if (manifest_.isOpen()) manifest_.close();
    setCapturing(false);

    const double avgMs = count_ > 1 ? static_cast<double>(lastTs_ - firstTs_) / (count_ - 1) : 0;
    const QString summary = tr("截图 %1 张, 实际平均间隔 %2 ms, 保存于 %3")
                                .arg(count_)
                                .arg(avgMs, 0, 'f', 0)
                                .arg(outDir_);
    statusLabel_->setText((cancelled ? tr("已取消。 ") : tr("完成。 ")) + summary);
    appendLog(cancelled ? "warn" : "info", (cancelled ? tr("采集已取消: ") : tr("采集完成: ")) + summary);

    if (count_ > 0) {
        lastDir_ = outDir_;
        sendBtn_->setEnabled(true);
        if (!cancelled && autoSendCheck_->isChecked()) emit sendToEval(lastDir_);
    }
}

void CaptureTab::onSendClicked() {
    if (!lastDir_.isEmpty()) emit sendToEval(lastDir_);
}
