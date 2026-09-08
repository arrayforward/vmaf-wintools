#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QCheckBox;
class QSpinBox;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
class QLabel;
class QLineEdit;

// 采集: 通过 adb 对 Android 设备定时截屏, 输出递增命名 PNG 序列 (供评测 Tab 作为目标输入)
class CaptureTab : public QWidget {
    Q_OBJECT
public:
    explicit CaptureTab(QWidget* parent = nullptr);
    ~CaptureTab() override;

signals:
    // 采集完成且勾选了"送评测"时发出, dir 为截图输出文件夹
    void sendToEval(const QString& dir);

private slots:
    void browseAdb();
    void refreshDevices();
    void browseOutDir();
    void startCapture();
    void stopCapture();
    void onTick();
    void onProcFinished(int exitCode, QProcess::ExitStatus status);
    void onSendClicked();

private:
    void appendLog(const QString& level, const QString& msg);
    void setCapturing(bool capturing);
    void finishCapture(bool cancelled);
    QString defaultOutDir() const;

    QLineEdit* adbEdit_;
    QComboBox* deviceCombo_;
    QPushButton* refreshBtn_;
    QSpinBox* intervalSpin_;
    QSpinBox* durationSpin_;
    QLineEdit* outDirEdit_;
    QCheckBox* autoSendCheck_;
    QPushButton* startBtn_;
    QPushButton* stopBtn_;
    QPushButton* sendBtn_;
    QProgressBar* progress_;
    QLabel* statusLabel_;
    QLabel* previewLabel_;
    QPlainTextEdit* log_;

    QTimer* timer_;
    QElapsedTimer elapsed_;
    QProcess* proc_ = nullptr;
    qint64 procStartMs_ = 0;  // 当前截图进程启动时刻 (elapsed_ 基准)
    bool stopping_ = false; // 到时停止, 等待最后一张飞完
    QFile manifest_;
    QString outDir_;
    QString lastDir_; // 最近一次采集输出文件夹
    int count_ = 0;
    qint64 firstTs_ = 0, lastTs_ = 0;
};
