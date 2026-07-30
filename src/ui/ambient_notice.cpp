#include "ui/ambient_notice.hpp"

#include "ui/theme.hpp"

#include <QFontMetrics>
#include <QPainter>

namespace mimi::ui {

AmbientNotice::AmbientNotice(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedHeight(36);
    setVisible(false);
}

QSize AmbientNotice::sizeHint() const {
    QFont f = font();
    f.setPixelSize(12);
    return {QFontMetrics(f).horizontalAdvance(text_) + 60, 36};
}

void AmbientNotice::notice(const QString& text) {
    text_ = text;
    setFixedWidth(sizeHint().width());
    setVisible(!text.isEmpty());
    update();
}

void AmbientNotice::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF body = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal r = body.height() / 2.0;
    QColor glass = theme::kLayer2;
    glass.setAlpha(210);
    p.setPen(Qt::NoPen);
    p.setBrush(glass);
    p.drawRoundedRect(body, r, r);
    p.setPen(QPen(QColor(255, 255, 255, 16), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, r, r);

    // Sparkle: her observation.
    p.setBrush(theme::kAccent);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(body.left() + 20, body.center().y()), 3.0, 3.0);

    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    p.setPen(theme::kDim);
    p.drawText(body.adjusted(34, 0, -18, 0), Qt::AlignVCenter | Qt::AlignLeft, text_);
}

}  // namespace mimi::ui
