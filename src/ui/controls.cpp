#include "ui/controls.hpp"

#include "ui/theme.hpp"

#include <QPainter>
#include <QVariantAnimation>

namespace mimi::ui {
namespace {

QColor mix(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QVariantAnimation* hoverAnimation(QObject* owner, qreal* value, QWidget* repaint) {
    auto* anim = new QVariantAnimation(owner);
    anim->setDuration(theme::kMotionMs);
    anim->setEasingCurve(theme::kMotion);
    QObject::connect(anim, &QVariantAnimation::valueChanged, repaint,
                     [value, repaint](const QVariant& v) {
                         *value = v.toReal();
                         repaint->update();
                     });
    return anim;
}

void retarget(QVariantAnimation* anim, qreal from, qreal to) {
    anim->stop();
    anim->setStartValue(from);
    anim->setEndValue(to);
    anim->start();
}

}  // namespace

// ------------------------------------------------------------- GhostButton

GhostButton::GhostButton(icons::Glyph glyph, QWidget* parent)
    : QAbstractButton(parent), glyph_(glyph) {
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
    anim_ = hoverAnimation(this, &hover_, this);
}

void GhostButton::setGlyph(icons::Glyph glyph) {
    glyph_ = glyph;
    update();
}

QSize GhostButton::sizeHint() const { return {34, 34}; }

void GhostButton::enterEvent(QEnterEvent*) { animateTo(1.0); }
void GhostButton::leaveEvent(QEvent*) { animateTo(0.0); }
void GhostButton::animateTo(qreal target) { retarget(anim_, hover_, target); }

void GhostButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool on = isChecked();
    if (on || hover_ > 0.01) {
        painter.setPen(Qt::NoPen);
        QColor fill = on ? theme::kAccent : QColor(255, 255, 255);
        fill.setAlphaF(on ? 0.14 : 0.07 * hover_);
        painter.setBrush(fill);
        painter.drawRoundedRect(rect(), 9, 9);
    }

    QColor tint = on ? theme::kAccentSoft : mix(theme::kDim, theme::kInk, hover_);
    if (!isEnabled()) tint = theme::kFaint;
    icons::icon(glyph_, tint, 20).paint(&painter,
                                        QRect((width() - 20) / 2, (height() - 20) / 2, 20, 20));
}

// -------------------------------------------------------------------- Chip

Chip::Chip(const QString& text, QWidget* parent) : QAbstractButton(parent) {
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(38);
    anim_ = hoverAnimation(this, &hover_, this);
}

QSize Chip::sizeHint() const {
    QFont font = this->font();
    font.setPixelSize(13);
    return {QFontMetrics(font).horizontalAdvance(text()) + 44, 38};
}

void Chip::enterEvent(QEnterEvent*) { animateTo(1.0); }
void Chip::leaveEvent(QEvent*) { animateTo(0.0); }
void Chip::animateTo(qreal target) { retarget(anim_, hover_, target); }

void Chip::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // The whole chip rises one pixel as the cursor arrives; the shadow gap it
    // leaves behind is the elevation cue, so no outline is needed.
    const qreal lift = hover_ * 1.0;
    const QRectF body = QRectF(rect()).adjusted(0.5, 0.5 - lift, -0.5, -0.5 - lift);

    painter.setPen(Qt::NoPen);
    QColor fill(255, 255, 255);
    fill.setAlphaF(isDown() ? 0.10 : 0.04 + 0.05 * hover_);
    painter.setBrush(fill);
    painter.drawRoundedRect(body, 19, 19);

    QFont font = painter.font();
    font.setPixelSize(13);
    painter.setFont(font);
    painter.setPen(mix(theme::kDim, theme::kInk, hover_));
    painter.drawText(body, Qt::AlignCenter, text());
}

}  // namespace mimi::ui
