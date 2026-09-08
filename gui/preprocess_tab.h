#pragma once

#include <QProcess>
#include <QWidget>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;

// 预处理: 片头插入绿屏 + 转码 (分辨率/码率适配客户端屏幕)
class PreprocessTab : public QWidget {
    Q_OBJECT
public:
    explicit PreprocessTab(QWidget* parent = nullptr);

private slots:
    void browseInput();
    void browseOutput();
    void startProcess();
    void cancelProcess();
    void onProcOutput();
    void onProcFinished(int exitCode);
    void startMarkerBurn();
    void onMarkerOutput();
    void onMarkerFinished(int exitCode);

private:
    QString findFfmpeg() const;
    void appendLog(const QString& text);

    QLineEdit* inputEdit_;
    QLineEdit* outputEdit_;
    QLineEdit* ffmpegEdit_;
    QCheckBox* greenCheck_;
    QSpinBox* greenSecSpin_;
    QComboBox* resCombo_;
    QSpinBox* bitrateSpin_;
    QPushButton* startBtn_;
    QPushButton* cancelBtn_;
    QProgressBar* progress_;
    QPlainTextEdit* log_;
    QProcess* proc_ = nullptr;
    double durationSec_ = 0;

    // 生成帧号片源 (调用同目录 marker_burn.exe)
    QLineEdit* markerInputEdit_;
    QLineEdit* markerOutputEdit_;
    QSpinBox* markerCrfSpin_;
    QPushButton* markerStartBtn_;
    QProcess* markerProc_ = nullptr;
    qint64 markerTotal_ = 0;
};
