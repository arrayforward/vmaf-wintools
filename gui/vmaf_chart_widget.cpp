#include "vmaf_chart_widget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

VmafChartWidget::VmafChartWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(120);
    setMouseTracking(false);
}

void VmafChartWidget::clear() {
    scores_.clear();
    dropped_.clear();
    current_ = -1;
    viewStart_ = 0;
    viewSpan_ = 0;
    update();
}

void VmafChartWidget::appendFrame(double score, bool dropped) {
    scores_.append(dropped ? -1 : score);
    dropped_.append(dropped);
    update();
}

void VmafChartWidget::updateFrame(int idx, double score) {
    if (idx >= 0 && idx < scores_.size()) {
        scores_[idx] = score;
        update();
    }
}

void VmafChartWidget::setCurrentFrame(int idx) {
    current_ = idx;
    update();
}

QRectF VmafChartWidget::plotArea() const {
    return QRectF(40, 10, width() - 50, height() - 35);
}

double VmafChartWidget::frameToX(int idx) const {
    const QRectF a = plotArea();
    const double span = viewSpan_ > 0 ? viewSpan_ : qMax(1, scores_.size());
    return a.left() + (idx - viewStart_) / span * a.width();
}

int VmafChartWidget::xToFrame(int x) const {
    const QRectF a = plotArea();
    const double span = viewSpan_ > 0 ? viewSpan_ : qMax(1, scores_.size());
    return static_cast<int>(viewStart_ + (x - a.left()) / a.width() * span + 0.5);
}

void VmafChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(25, 25, 28));
    const QRectF a = plotArea();

    // 网格与 Y 轴刻度
    p.setPen(QColor(70, 70, 75));
    for (int s = 0; s <= 100; s += 20) {
        const double y = a.bottom() - s / 100.0 * a.height();
        p.drawLine(QPointF(a.left(), y), QPointF(a.right(), y));
        p.setPen(QColor(160, 160, 160));
        p.drawText(QRectF(0, y - 8, 34, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(s));
        p.setPen(QColor(70, 70, 75));
    }

    if (scores_.isEmpty()) {
        p.setPen(Qt::gray);
        p.drawText(a, Qt::AlignCenter, tr("VMAF 曲线 (评估后显示)"));
        return;
    }

    const int n = scores_.size();
    const double span = viewSpan_ > 0 ? viewSpan_ : n;
    const int i0 = qBound(0, static_cast<int>(viewStart_), n - 1);
    const int i1 = qBound(0, static_cast<int>(viewStart_ + span) + 1, n - 1);

    // 丢帧标记
    p.setPen(QPen(QColor(220, 60, 60), 1));
    for (int i = i0; i <= i1; ++i) {
        if (dropped_[i]) {
            const double x = frameToX(i);
            p.drawLine(QPointF(x, a.top()), QPointF(x, a.bottom()));
        }
    }

    // 折线
    p.setPen(QPen(QColor(80, 180, 255), 1.5));
    QPointF prev;
    bool hasPrev = false;
    for (int i = i0; i <= i1; ++i) {
        if (scores_[i] < 0) {
            hasPrev = false;
            continue;
        }
        const QPointF pt(frameToX(i), a.bottom() - scores_[i] / 100.0 * a.height());
        if (hasPrev) p.drawLine(prev, pt);
        prev = pt;
        hasPrev = true;
    }

    // 当前帧标记
    if (current_ >= 0 && current_ < n) {
        const double x = frameToX(current_);
        p.setPen(QPen(QColor(255, 200, 60), 1.5));
        p.drawLine(QPointF(x, a.top()), QPointF(x, a.bottom()));
        if (scores_[current_] >= 0) {
            p.setBrush(QColor(255, 200, 60));
            p.drawEllipse(QPointF(x, a.bottom() - scores_[current_] / 100.0 * a.height()), 4, 4);
        }
        p.setPen(Qt::white);
        p.drawText(QRectF(x + 6, a.top(), 200, 16),
                   QString("帧 %1  VMAF=%2").arg(current_).arg(scores_[current_], 0, 'f', 2));
    }

    // X 轴标签
    p.setPen(QColor(160, 160, 160));
    p.drawText(QRectF(a.left(), a.bottom() + 4, 100, 16), QString::number(i0));
    p.drawText(QRectF(a.right() - 100, a.bottom() + 4, 100, 16), Qt::AlignRight,
               QString::number(i1));
}

void VmafChartWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        const int idx = xToFrame(e->pos().x());
        if (idx >= 0 && idx < scores_.size()) {
            setCurrentFrame(idx);
            emit frameSelected(idx);
        }
    } else if (e->button() == Qt::RightButton) {
        panning_ = true;
        panStart_ = e->pos();
        panStartView_ = viewStart_;
    }
}

void VmafChartWidget::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        const QRectF a = plotArea();
        const double span = viewSpan_ > 0 ? viewSpan_ : qMax(1, scores_.size());
        const double delta = (e->pos().x() - panStart_.x()) / a.width() * span;
        viewStart_ = qBound(0.0, panStartView_ - delta,
                            qMax(0.0, scores_.size() - span));
        update();
    }
}

void VmafChartWidget::wheelEvent(QWheelEvent* e) {
    const int n = scores_.size();
    if (n == 0) return;
    const double oldSpan = viewSpan_ > 0 ? viewSpan_ : n;
    const double factor = e->angleDelta().y() > 0 ? 0.8 : 1.25;
    double newSpan = qBound(20.0, oldSpan * factor, static_cast<double>(n));
    const QRectF a = plotArea();
    const double anchor = viewStart_ + (e->position().x() - a.left()) / a.width() * oldSpan;
    viewStart_ = qBound(0.0, anchor - (anchor - viewStart_) * (newSpan / oldSpan),
                        qMax(0.0, n - newSpan));
    viewSpan_ = newSpan >= n ? 0 : newSpan;
    update();
}
