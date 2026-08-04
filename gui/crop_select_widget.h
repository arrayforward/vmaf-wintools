#pragma once

#include <QImage>
#include <QWidget>

// 显示一帧图像, 支持鼠标拖拽框选裁剪区域 (坐标以原始像素返回)
class CropSelectWidget : public QWidget {
    Q_OBJECT
public:
    explicit CropSelectWidget(QWidget* parent = nullptr);

    void setImage(const QImage& img);       // 原始尺寸图像
    void setCropRect(const QRect& rect);    // 原始像素坐标; 空 rect = 自动中心1/2
    QRect cropRect() const { return crop_; }
    void setInteractive(bool on) { interactive_ = on; }
    QSize imageSize() const { return img_.size(); }

signals:
    void cropChanged(const QRect& rect);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QRectF toWidget(const QRect& r) const;
    QRect toImage(const QRectF& r) const;
    QRect defaultCenterRect() const;
    QRect clampToImage(QRect r) const;

    QImage img_;
    QRect crop_;       // 原始像素坐标, 空 = 自动中心
    bool interactive_ = true;
    bool dragging_ = false;
    QPoint dragStart_;
    QRect dragCurrent_; // widget 坐标
};

