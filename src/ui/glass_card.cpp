#include "ui/glass_card.hpp"

#include "ui/theme.hpp"

#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QVariantAnimation>

#include <algorithm>

namespace mimi::ui {

GlassCard::GlassCard(const QString& tag, const QString& title, const QString& body,
                     QWidget* parent)
    : QWidget(parent), tag_(tag), title_(title), body_(body) {
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(96);

    shadow_ = new QGraphicsDropShadowEffect(this);
    shadow_->setColor(QColor(0, 0, 0, 150));
    shadow_->setBlurRadius(24);
    shadow_->setOffset(0, 10);
    setGraphicsEffect(shadow_);

    anim_ = new QVariantAnimation(this);
    anim_->setDuration(theme::kMotionMs);
    anim_->setEasingCurve(theme::kMotion);
    connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        hover_ = v.toReal();
        // The shadow deepens and drops as the card rises toward the cursor.
        shadow_->setBlurRadius(24 + 18 * hover_);
        shadow_->setOffset(-light_.x() * 8 * hover_, 10 + 8 * hover_);
        update();
    });
}

void GlassCard::setBody(const QString& body) {
    body_ = body;
    update();
}

void GlassCard::enterEvent(QEnterEvent*) { animateHover(1.0); }
void GlassCard::leaveEvent(QEvent*) { animateHover(0.0); }

void GlassCard::animateHover(qreal to) {
    anim_->stop();
    anim_->setStartValue(hover_);
    anim_->setEndValue(to);
    anim_->start();
}

void GlassCard::mouseMoveEvent(QMouseEvent* event) {
    light_ = QPointF(std::clamp(event->position().x() / width() * 2.0 - 1.0, -1.0, 1.0),
                     std::clamp(event->position().y() / height() * 2.0 - 1.0, -1.0, 1.0));
    shadow_->setOffset(-light_.x() * 8 * hover_, 10 + 8 * hover_);
    update();
}

void GlassCard::mouseReleaseEvent(QMouseEvent* event) {
    if (rect().contains(event->pos())) Q_EMIT clicked();
}

void GlassCard::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal lift = hover_ * 4.0;
    const QRectF body = QRectF(rect()).adjusted(1, 1 - lift, -1, -1 - lift);

    // Glass.
    QColor glass = theme::kLayer2;
    glass.setAlpha(238);
    if (hover_ > 0.01) glass = glass.lighter(static_cast<int>(100 + 12 * hover_));
    painter.setPen(Qt::NoPen);
    painter.setBrush(glass);
    painter.drawRoundedRect(body, 16, 16);

    // Specular highlight sliding toward the cursor -- the tell that it is glass
    // catching light, not a painted rectangle.
    if (hover_ > 0.01) {
        const QPointF spec(body.center().x() + light_.x() * body.width() * 0.35,
                           body.top() + body.height() * 0.15 + light_.y() * body.height() * 0.2);
        QRadialGradient g(spec, body.width() * 0.7);
        QColor hi(255, 255, 255);
        hi.setAlphaF(0.06 * hover_);
        g.setColorAt(0.0, hi);
        hi.setAlpha(0);
        g.setColorAt(1.0, hi);
        painter.setBrush(g);
        painter.drawRoundedRect(body, 16, 16);
    }

    // Rim.
    painter.setPen(QPen(QColor(255, 255, 255, hover_ > 0.01 ? 34 : 18), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(body, 16, 16);

    // Tag dot + label.
    painter.setBrush(theme::kAccent);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(body.left() + 20, body.top() + 24), 3.0, 3.0);

    QFont caps = font();
    caps.setPixelSize(14);
    caps.setWeight(QFont::DemiBold);
    caps.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    painter.setFont(caps);
    QColor tagColour = theme::kAccentSoft;
    painter.setPen(tagColour);
    painter.drawText(QRectF(body.left() + 30, body.top() + 16, body.width() - 46, 16),
                     Qt::AlignVCenter | Qt::AlignLeft, tag_);

    QFont titleFont = font();
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(theme::kInk);
    painter.drawText(QRectF(body.left() + 20, body.top() + 34, body.width() - 40, 24),
                     Qt::AlignVCenter | Qt::AlignLeft, title_);

    QFont bodyFont = font();
    bodyFont.setPixelSize(17);
    painter.setFont(bodyFont);
    painter.setPen(theme::kDim);
    painter.drawText(QRectF(body.left() + 20, body.top() + 58, body.width() - 40,
                            body.height() - 66),
                     Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, body_);
}

}  // namespace mimi::ui
