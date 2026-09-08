#include "main_window.h"

#include "capture_tab.h"
#include "eval_tab.h"
#include "preprocess_tab.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("VMAF 画质评估工具"));
    resize(1280, 860);
    tabs_ = new QTabWidget(this);
    auto* evalTab = new EvalTab(this);
    auto* captureTab = new CaptureTab(this);
    tabs_->addTab(captureTab, tr("采集"));
    tabs_->addTab(evalTab, tr("画质评估"));
    tabs_->addTab(new PreprocessTab(this), tr("视频预处理 (绿屏/转码)"));
    setCentralWidget(tabs_);

    // 采集完成 → 截图文件夹填入评测目标输入并触发评估
    connect(captureTab, &CaptureTab::sendToEval, this, [this, evalTab](const QString& dir) {
        evalTab->setDistInput(dir);
        tabs_->setCurrentWidget(evalTab);
        evalTab->startEvaluation();
    });
}
