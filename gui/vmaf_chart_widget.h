#pragma once

#include <QWidget>
#include <QVector>

// 逐帧 VMAF 折线图, 支持点击跳转 / 滚轮缩放 / 拖动平移
// 只接收有效样本 (丢帧/无分数的帧不进图表), 折线直接连接相邻有效样本
class VmafChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit VmafChartWidget(QWidget* parent = nullptr);

    void clear();
    // 添加一个有效样本; frameNo 为 x 轴标注值 (视频模式=参考帧号, 采样模式=样本序号)
    void appendFrame(double score, int frameNo);
    void updateFrame(int idx, double score); // 补充分数
    void setCurrentFrame(int idx);
    void setXAxisTitle(const QString& title); // x 轴语义标注
    void setYAxisTitle(const QString& title); // y 轴单位标注 (如 "VMAF" / "dB")
    int frameCount() const { return scores_.size(); }
    double scoreAt(int idx) const { return scores_.value(idx, -1); }

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

    QVector<double> scores_;   // -1 = 分数未就绪 (延迟一帧回填)
    QVector<int> frameNos_;    // 每个样本的 x 轴标注值
    QString xTitle_;
    QString yTitle_;
    int current_ = -1;
    double viewStart_ = 0; // 可见起始帧
    double viewSpan_ = 0;  // 可见帧数, 0 = 全部
    bool panning_ = false;
    QPoint panStart_;
    double panStartView_ = 0;
};
