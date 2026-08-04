#include "main_window.h"

#include "eval_tab.h"
#include "preprocess_tab.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("VMAF 画质评估工具"));
    resize(1280, 860);
    tabs_ = new QTabWidget(this);
    tabs_->addTab(new EvalTab(this), tr("画质评估"));
    tabs_->addTab(new PreprocessTab(this), tr("视频预处理 (绿屏/转码)"));
    setCentralWidget(tabs_);
}
