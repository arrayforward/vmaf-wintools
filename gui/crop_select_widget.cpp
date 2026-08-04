#include "crop_select_widget.h"

#include <QMouseEvent>
#include <QPainter>

CropSelectWidget::CropSelectWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(240, 135);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void CropSelectWidget::setImage(const QImage& img) {
    img_ = img;
    crop_ = QRect();
    update();
}

void CropSelectWidget::setCropRect(const QRect& rect) {
    crop_ = rect;
    update();
}

QRect CropSelectWidget::defaultCenterRect() const {
    const int w = (img_.width() / 2) & ~1;
    const int h = (img_.height() / 2) & ~1;
    return QRect(((img_.width() - w) / 2) & ~1, ((img_.height() - h) / 2) & ~1, w, h);
}

QRect CropSelectWidget::clampToImage(QRect r) const {
    return r.intersected(QRect(0, 0, img_.width(), img_.height()));
}

QRectF CropSelectWidget::toWidget(const QRect& r) const {
    if (img_.isNull()) return QRectF();
    const double sx = static_cast<double>(width()) / img_.width();
    const double sy = static_cast<double>(height()) / img_.height();
    const double s = qMin(sx, sy);
    const double ox = (width() - img_.width() * s) / 2;
    const double oy = (height() - img_.height() * s) / 2;
    return QRectF(ox + r.x() * s, oy + r.y() * s, r.width() * s, r.height() * s);
}

QRect CropSelectWidget::toImage(const QRectF& r) const {
    if (img_.isNull()) return QRect();
    const double sx = static_cast<double>(width()) / img_.width();
    const double sy = static_cast<double>(height()) / img_.height();
    const double s = qMin(sx, sy);
    const double ox = (width() - img_.width() * s) / 2;
    const double oy = (height() - img_.height() * s) / 2;
    return QRect(static_cast<int>((r.x() - ox) / s + 0.5), static_cast<int>((r.y() - oy) / s + 0.5),
                 static_cast<int>(r.width() / s + 0.5), static_cast<int>(r.height() / s + 0.5));
}

void CropSelectWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 30));
    if (img_.isNull()) {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, tr("未加载视频"));
        return;
    }
    const QRectF target = toWidget(QRect(0, 0, img_.width(), img_.height()));
    p.drawImage(target, img_);

    const QRect sel = crop_.isValid() && !crop_.isNull() ? crop_ : defaultCenterRect();
    const QRectF selW = toWidget(sel);
    p.fillRect(selW, QColor(0, 170, 255, 40));
    p.setPen(QPen(QColor(0, 170, 255), 2, Qt::DashLine));
    p.drawRect(selW);
    p.setPen(Qt::white);
    p.drawText(selW.adjusted(4, 2, 0, 0), Qt::AlignTop | Qt::AlignLeft,
               QString("%1,%2 %3x%4").arg(sel.x()).arg(sel.y()).arg(sel.width()).arg(sel.height()));
}

void CropSelectWidget::mousePressEvent(QMouseEvent* e) {
    if (!interactive_ || img_.isNull()) return;
    if (e->button() == Qt::LeftButton) {
        dragging_ = true;
        dragStart_ = e->pos();
        dragCurrent_ = QRect(dragStart_, QSize(1, 1));
    }
}

void CropSelectWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return;
    dragCurrent_ = QRect(dragStart_, e->pos()).normalized();
    update();
}

void CropSelectWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragging_) return;
    dragging_ = false;
    const QRect imgRect = clampToImage(toImage(QRectF(dragCurrent_)));
    if (imgRect.width() >= 8 && imgRect.height() >= 8) {
        crop_ = imgRect;
        emit cropChanged(crop_);
    }
    update();
}
