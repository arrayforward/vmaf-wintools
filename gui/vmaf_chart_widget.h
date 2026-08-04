#pragma once

#include <QWidget>
#include <QVector>

// 逐帧 VMAF 折线图, 支持点击跳转 / 滚轮缩放 / 拖动平移
class VmafChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit VmafChartWidget(QWidget* parent = nullptr);

    void clear();
    // 添加一帧 (dropped 帧 score 忽略)
    void appendFrame(double score, bool dropped);
    void updateFrame(int idx, double score); // 补充分数
    void setCurrentFrame(int idx);
    int frameCount() const { return scores_.size(); }
    double scoreAt(int idx) const { return scores_.value(idx, -1); }
    bool droppedAt(int idx) const { return dropped_.value(idx, false); }

signals:
    void frameSelected(int idx);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    QRectF plotArea() const;
    int xToFrame(int x) const;
    double frameToX(int idx) const;

    QVector<double> scores_; // -1 = 无分数
    QVector<bool> dropped_;
    int current_ = -1;
    double viewStart_ = 0; // 可见起始帧
    double viewSpan_ = 0;  // 可见帧数, 0 = 全部
    bool panning_ = false;
    QPoint panStart_;
    double panStartView_ = 0;
};
