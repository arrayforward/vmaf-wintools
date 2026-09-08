#include "preprocess_tab.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

PreprocessTab::PreprocessTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    auto* inputRow = new QHBoxLayout;
    inputEdit_ = new QLineEdit;
    auto* inBtn = new QPushButton(tr("浏览…"));
    inputRow->addWidget(inputEdit_);
    inputRow->addWidget(inBtn);
    form->addRow(tr("原始视频:"), inputRow);

    auto* outputRow = new QHBoxLayout;
    outputEdit_ = new QLineEdit;
    auto* outBtn = new QPushButton(tr("浏览…"));
    outputRow->addWidget(outputEdit_);
    outputRow->addWidget(outBtn);
    form->addRow(tr("输出文件:"), outputRow);

    auto* ffRow = new QHBoxLayout;
    ffmpegEdit_ = new QLineEdit(findFfmpeg());
    auto* ffBtn = new QPushButton(tr("浏览…"));
    ffRow->addWidget(ffmpegEdit_);
    ffRow->addWidget(ffBtn);
    form->addRow(tr("ffmpeg 路径:"), ffRow);

    auto* greenRow = new QHBoxLayout;
    greenCheck_ = new QCheckBox(tr("片头插入绿屏"));
    greenCheck_->setChecked(true);
    greenSecSpin_ = new QSpinBox;
    greenSecSpin_->setRange(1, 60);
    greenSecSpin_->setValue(5);
    greenSecSpin_->setSuffix(tr(" 秒"));
    greenRow->addWidget(greenCheck_);
    greenRow->addWidget(greenSecSpin_);
    greenRow->addStretch();
    form->addRow(tr("绿屏标定:"), greenRow);

    resCombo_ = new QComboBox;
    resCombo_->addItem(tr("1920x1080 (默认 1K)"), QSize(1920, 1080));
    resCombo_->addItem("1280x720", QSize(1280, 720));
    resCombo_->addItem("2560x1440", QSize(2560, 1440));
    resCombo_->addItem("3840x2160", QSize(3840, 2160));
    resCombo_->addItem(tr("保持原始"), QSize(0, 0));
    form->addRow(tr("输出分辨率:"), resCombo_);

    bitrateSpin_ = new QSpinBox;
    bitrateSpin_->setRange(200, 100000);
    bitrateSpin_->setValue(5000);
    bitrateSpin_->setSingleStep(500);
    bitrateSpin_->setSuffix(" kbps");
    form->addRow(tr("视频码率:"), bitrateSpin_);

    layout->addLayout(form);

    // ---- 生成帧号片源 (烧录 QR 帧号, 供评测端精确对齐) ----
    auto* markerGroup = new QGroupBox(tr("生成帧号片源 (逐帧烧录 QR 帧号)"));
    auto* markerForm = new QFormLayout(markerGroup);
    auto* mInRow = new QHBoxLayout;
    markerInputEdit_ = new QLineEdit;
    auto* mInBtn = new QPushButton(tr("浏览…"));
    mInRow->addWidget(markerInputEdit_);
    mInRow->addWidget(mInBtn);
    markerForm->addRow(tr("输入视频:"), mInRow);

    auto* mOutRow = new QHBoxLayout;
    markerOutputEdit_ = new QLineEdit;
    auto* mOutBtn = new QPushButton(tr("浏览…"));
    mOutRow->addWidget(markerOutputEdit_);
    mOutRow->addWidget(mOutBtn);
    markerForm->addRow(tr("输出文件:"), mOutRow);

    auto* mCrfRow = new QHBoxLayout;
    markerCrfSpin_ = new QSpinBox;
    markerCrfSpin_->setRange(0, 51);
    markerCrfSpin_->setValue(14);
    markerStartBtn_ = new QPushButton(tr("开始生成"));
    mCrfRow->addWidget(new QLabel(tr("CRF (越小越清晰):")));
    mCrfRow->addWidget(markerCrfSpin_);
    mCrfRow->addWidget(markerStartBtn_);
    mCrfRow->addStretch();
    markerForm->addRow(mCrfRow);
    layout->addWidget(markerGroup);

    auto* btnRow = new QHBoxLayout;
    startBtn_ = new QPushButton(tr("开始处理"));
    cancelBtn_ = new QPushButton(tr("取消"));
    cancelBtn_->setEnabled(false);
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    btnRow->addWidget(startBtn_);
    btnRow->addWidget(cancelBtn_);
    btnRow->addWidget(progress_, 1);
    layout->addLayout(btnRow);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    layout->addWidget(log_, 1);

    connect(inBtn, &QPushButton::clicked, this, &PreprocessTab::browseInput);
    connect(outBtn, &QPushButton::clicked, this, &PreprocessTab::browseOutput);
    connect(ffBtn, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("选择 ffmpeg"), QString(),
                                                       "ffmpeg (ffmpeg.exe ffmpeg)");
        if (!f.isEmpty()) ffmpegEdit_->setText(f);
    });
    connect(startBtn_, &QPushButton::clicked, this, &PreprocessTab::startProcess);
    connect(cancelBtn_, &QPushButton::clicked, this, &PreprocessTab::cancelProcess);

    connect(mInBtn, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(
            this, tr("选择输入视频"), QString(),
            tr("视频文件 (*.mp4 *.mkv *.avi *.mov *.flv);;所有文件 (*)"));
        if (f.isEmpty()) return;
        markerInputEdit_->setText(f);
        if (markerOutputEdit_->text().isEmpty()) {
            const QFileInfo fi(f);
            markerOutputEdit_->setText(fi.absolutePath() + "/" + fi.completeBaseName() +
                                       "_marked.mp4");
        }
    });
    connect(mOutBtn, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("输出文件"), QString(),
                                                       tr("MP4 视频 (*.mp4)"));
        if (!f.isEmpty()) markerOutputEdit_->setText(f);
    });
    connect(markerStartBtn_, &QPushButton::clicked, this, &PreprocessTab::startMarkerBurn);
}

QString PreprocessTab::findFfmpeg() const {
    // 优先使用 exe 同目录内置的 ffmpeg
    const QString bundled = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
    if (QFileInfo::exists(bundled)) return bundled;
    return QStandardPaths::findExecutable("ffmpeg");
}

void PreprocessTab::browseInput() {
    const QString f = QFileDialog::getOpenFileName(
        this, tr("选择原始视频"), QString(), tr("视频文件 (*.mp4 *.mkv *.avi *.mov *.flv);;所有文件 (*)"));
    if (f.isEmpty()) return;
    inputEdit_->setText(f);
    if (outputEdit_->text().isEmpty()) {
        const QFileInfo fi(f);
        outputEdit_->setText(fi.absolutePath() + "/" + fi.completeBaseName() + "_processed.mp4");
    }
}

void PreprocessTab::browseOutput() {
    const QString f = QFileDialog::getSaveFileName(this, tr("输出文件"), QString(),
                                                   tr("MP4 视频 (*.mp4)"));
    if (!f.isEmpty()) outputEdit_->setText(f);
}

void PreprocessTab::appendLog(const QString& text) {
    log_->appendPlainText(text);
}

void PreprocessTab::startProcess() {
    const QString ffmpeg = ffmpegEdit_->text();
    const QString input = inputEdit_->text();
    const QString output = outputEdit_->text();
    if (input.isEmpty() || output.isEmpty()) {
        appendLog(tr("[错误] 请选择输入和输出文件"));
        return;
    }
    if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) {
        appendLog(tr("[错误] 找不到 ffmpeg, 请指定路径"));
        return;
    }

    // 用 ffprobe 拿源分辨率和帧率
    QString ffprobe = ffmpeg;
    ffprobe.replace("ffmpeg", "ffprobe");
    int srcW = 0, srcH = 0;
    double srcFps = 30.0;
    durationSec_ = 0;
    if (QFileInfo::exists(ffprobe)) {
        QProcess probe;
        probe.start(ffprobe, {"-v", "quiet", "-print_format", "json", "-show_streams",
                              "-show_format", input});
        probe.waitForFinished(15000);
        const QJsonDocument doc = QJsonDocument::fromJson(probe.readAllStandardOutput());
        for (const auto& v : doc.object()["streams"].toArray()) {
            const QJsonObject s = v.toObject();
            if (s["codec_type"].toString() == "video") {
                srcW = s["width"].toInt();
                srcH = s["height"].toInt();
                const QStringList parts = s["avg_frame_rate"].toString().split('/');
                if (parts.size() == 2 && parts[1].toDouble() > 0)
                    srcFps = parts[0].toDouble() / parts[1].toDouble();
                break;
            }
        }
        durationSec_ = doc.object()["format"].toObject()["duration"].toString().toDouble();
    }

    const QSize outSize = resCombo_->currentData().toSize();
    const int outW = outSize.width() > 0 ? outSize.width() : srcW;
    const int outH = outSize.height() > 0 ? outSize.height() : srcH;
    const QString bitrate = QString::number(bitrateSpin_->value()) + "k";

    QStringList args;
    args << "-y" << "-i" << input;
    if (greenCheck_->isChecked()) {
        if (outW <= 0 || outH <= 0) {
            appendLog(tr("[错误] 无法获取源视频分辨率, 无法生成绿屏"));
            return;
        }
        args << "-f" << "lavfi" << "-i"
             << QString("color=green:s=%1x%2:r=%3:d=%4")
                    .arg(outW)
                    .arg(outH)
                    .arg(srcFps, 0, 'f', 3)
                    .arg(greenSecSpin_->value());
        args << "-filter_complex"
             << QString("[0:v]scale=%1:%2,fps=%3[main];[1:v][main]concat=n=2:v=1[out]")
                    .arg(outW)
                    .arg(outH)
                    .arg(srcFps, 0, 'f', 3)
             << "-map" << "[out]";
    } else if (outW > 0 && outH > 0 && (outW != srcW || outH != srcH)) {
        args << "-vf" << QString("scale=%1:%2").arg(outW).arg(outH);
    }
    args << "-c:v" << "libx264" << "-preset" << "medium" << "-b:v" << bitrate << "-pix_fmt"
         << "yuv420p" << "-an" << output;

    appendLog(tr("[命令] ffmpeg %1").arg(args.join(' ')));

    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc_, &QProcess::readyReadStandardOutput, this, &PreprocessTab::onProcOutput);
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) { onProcFinished(code); });
    startBtn_->setEnabled(false);
    markerStartBtn_->setEnabled(false);
    cancelBtn_->setEnabled(true);
    progress_->setValue(0);
    proc_->start(ffmpeg, args);
}

void PreprocessTab::onProcOutput() {
    const QString out = QString::fromLocal8Bit(proc_->readAllStandardOutput());
    // 解析 time= 进度
    static const QRegularExpression re("time=(\\d+):(\\d+):([\\d.]+)");
    const auto m = re.match(out);
    if (m.hasMatch() && durationSec_ > 0) {
        const double t = m.captured(1).toInt() * 3600 + m.captured(2).toInt() * 60 +
                         m.captured(3).toDouble();
        progress_->setValue(qMin(100, static_cast<int>(t / durationSec_ * 100)));
    }
    for (const QString& line : out.split('\n', Qt::SkipEmptyParts))
        if (!line.contains("frame=")) appendLog(line.trimmed());
}

void PreprocessTab::onProcFinished(int exitCode) {
    appendLog(exitCode == 0 ? tr("[完成] 处理成功: %1").arg(outputEdit_->text())
                            : tr("[错误] ffmpeg 退出码 %1").arg(exitCode));
    if (exitCode == 0) progress_->setValue(100);
    startBtn_->setEnabled(true);
    markerStartBtn_->setEnabled(true);
    cancelBtn_->setEnabled(false);
    proc_->deleteLater();
    proc_ = nullptr;
}

void PreprocessTab::cancelProcess() {
    if (proc_) proc_->kill();
    if (markerProc_) markerProc_->kill();
}

// ---- 生成帧号片源 ----

void PreprocessTab::startMarkerBurn() {
    const QString input = markerInputEdit_->text();
    const QString output = markerOutputEdit_->text();
    if (input.isEmpty() || output.isEmpty()) {
        appendLog(tr("[错误] 请选择输入视频和输出文件"));
        return;
    }
    // marker_burn.exe 与本程序同目录构建
    const QString tool = QCoreApplication::applicationDirPath() + "/marker_burn.exe";
    if (!QFileInfo::exists(tool)) {
        appendLog(tr("[错误] 找不到 marker_burn.exe (应位于程序同目录: %1), 请先构建").arg(tool));
        return;
    }

    QStringList args;
    args << input << output << "--crf" << QString::number(markerCrfSpin_->value());
    appendLog(tr("[命令] marker_burn %1").arg(args.join(' ')));

    markerTotal_ = 0;
    markerProc_ = new QProcess(this);
    markerProc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(markerProc_, &QProcess::readyReadStandardOutput, this,
            &PreprocessTab::onMarkerOutput);
    connect(markerProc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) { onMarkerFinished(code); });
    markerStartBtn_->setEnabled(false);
    startBtn_->setEnabled(false);
    cancelBtn_->setEnabled(true);
    progress_->setValue(0);
    markerProc_->start(tool, args);
}

void PreprocessTab::onMarkerOutput() {
    // marker_burn 输出为 UTF-8 (/utf-8 编译), 日志与进度按 UTF-8 解码
    const QString out = QString::fromUtf8(markerProc_->readAllStandardOutput());
    static const QRegularExpression totalRe(QStringLiteral("总帧数 (\\d+)"));
    static const QRegularExpression progRe(QStringLiteral("已处理 (\\d+) 帧"));
    const auto tm = totalRe.match(out);
    if (tm.hasMatch()) markerTotal_ = tm.captured(1).toLongLong();
    // 一次可能读到多行进度, 取最后一行
    auto it = progRe.globalMatch(out);
    QRegularExpressionMatch pm;
    while (it.hasNext()) pm = it.next();
    if (pm.hasMatch() && markerTotal_ > 0)
        progress_->setValue(qMin(100, static_cast<int>(pm.captured(1).toLongLong() * 100 /
                                                      markerTotal_)));
    for (const QString& line : out.split('\n', Qt::SkipEmptyParts))
        appendLog(line.trimmed());
}

void PreprocessTab::onMarkerFinished(int exitCode) {
    appendLog(exitCode == 0 ? tr("[完成] 帧号片源生成成功: %1").arg(markerOutputEdit_->text())
                            : tr("[错误] marker_burn 退出码 %1").arg(exitCode));
    if (exitCode == 0) progress_->setValue(100);
    markerStartBtn_->setEnabled(true);
    startBtn_->setEnabled(true);
    cancelBtn_->setEnabled(false);
    markerProc_->deleteLater();
    markerProc_ = nullptr;
}
