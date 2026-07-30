#include "ui/controls.hpp"

#include "ui/theme.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

#include <algorithm>

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

QSize GhostButton::sizeHint() const { return {40, 40}; }

void GhostButton::enterEvent(QEnterEvent*) { animateTo(1.0); }
void GhostButton::leaveEvent(QEvent*) { animateTo(0.0); }
void GhostButton::animateTo(qreal target) { retarget(anim_, hover_, target); }

void GhostButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Selection is carried by the icon and a short rule beneath it, not by a
    // tinted capsule behind it. A filled pill per button turns a row of five
    // into five competing shapes; an editor's activity bar stays quiet and
    // marks exactly one thing.
    const bool on = isChecked();
    if (hover_ > 0.01) {
        painter.setPen(Qt::NoPen);
        QColor fill(255, 255, 255);
        fill.setAlphaF(0.055 * hover_);
        painter.setBrush(fill);
        painter.drawRoundedRect(rect(), 5, 5);
    }

    QColor tint = on ? theme::kInk : mix(theme::kFaint, theme::kDim, hover_);
    if (!isEnabled()) tint = theme::kFaint;
    constexpr int kGlyph = 21;
    icons::icon(glyph_, tint, kGlyph)
        .paint(&painter,
               QRect((width() - kGlyph) / 2, (height() - kGlyph) / 2 - 1, kGlyph, kGlyph));

    if (on) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme::kAccent);
        const int w = 16;
        painter.drawRoundedRect(QRectF((width() - w) / 2.0, height() - 3.0, w, 1.5), 0.75,
                                0.75);
    }
}

// -------------------------------------------------------- ConfidenceMeter

ConfidenceMeter::ConfidenceMeter(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    // A slow, eased fill: the bar catching up to the figure is the tell that a
    // judgement was made, not a value dumped on screen.
    anim_ = new QVariantAnimation(this);
    anim_->setDuration(620);
    anim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        shown_ = v.toReal();
        update();
    });
}

QSize ConfidenceMeter::sizeHint() const { return {470, 22}; }

void ConfidenceMeter::setConfidence(qreal value) {
    if (value < 0.0) {
        active_ = false;
        anim_->stop();
        shown_ = 0.0;
        update();
        return;
    }
    active_ = true;
    anim_->stop();
    anim_->setStartValue(shown_);
    anim_->setEndValue(std::clamp(value, 0.0, 1.0));
    anim_->start();
}

void ConfidenceMeter::paintEvent(QPaintEvent*) {
    if (!active_) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QFont caps = font();
    caps.setPixelSize(13);
    caps.setWeight(QFont::DemiBold);
    caps.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    painter.setFont(caps);
    painter.setPen(theme::kFaint);
    const int labelW = 88;
    painter.drawText(QRect(0, 0, labelW, height()), Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("CONFIDENCE"));

    // The track, then the accent fill over it, with a soft head where it stops.
    // Right side carries the figure plus the receipt: how many sources, and
    // whether the reasoning was verified.
    const qreal pctW = 38;
    const qreal detailW = 185;
    const QRectF track(labelW, height() / 2.0 - 2.0,
                       width() - labelW - pctW - detailW - 8, 4.0);
    QColor bed = theme::kFaint;
    bed.setAlphaF(0.28);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bed);
    painter.drawRoundedRect(track, 2, 2);

    QRectF fill = track;
    fill.setWidth(track.width() * shown_);
    painter.setBrush(theme::kAccent);
    painter.drawRoundedRect(fill, 2, 2);

    QColor glow = theme::kAccentGlow;
    glow.setAlphaF(0.9);
    painter.setBrush(glow);
    painter.drawEllipse(QPointF(fill.right(), track.center().y()), 2.6, 2.6);

    QFont num = font();
    num.setPixelSize(15);
    num.setWeight(QFont::DemiBold);
    painter.setFont(num);
    painter.setPen(theme::kDim);
    painter.drawText(QRect(static_cast<int>(track.right()) + 8, 0, pctW, height()),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("%1%").arg(qRound(shown_ * 100)));

    painter.setFont(caps);
    painter.setPen(theme::kFaint);
    painter.drawText(QRect(width() - static_cast<int>(detailW), 0, static_cast<int>(detailW),
                           height()),
                     Qt::AlignVCenter | Qt::AlignRight,
                     QStringLiteral("5 SOURCES · VERIFIED"));
}

// -------------------------------------------------------------- ModeToggle

ModeToggle::ModeToggle(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
    anim_ = new QVariantAnimation(this);
    anim_->setDuration(theme::kMotionMs);
    anim_->setEasingCurve(theme::kMotion);
    connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        pos_ = v.toReal();
        update();
    });
}

QSize ModeToggle::sizeHint() const { return {150, 28}; }

void ModeToggle::setExpert(bool expert) {
    if (expert == expert_) return;
    expert_ = expert;
    anim_->stop();
    anim_->setStartValue(pos_);
    anim_->setEndValue(expert_ ? 1.0 : 0.0);
    anim_->start();
    Q_EMIT toggled(expert_);
}

void ModeToggle::mouseReleaseEvent(QMouseEvent* event) {
    setExpert(event->position().x() >= width() / 2.0);
}

void ModeToggle::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF body = QRectF(rect());
    const qreal r = body.height() / 2.0;
    QColor bed(255, 255, 255);
    bed.setAlphaF(0.05);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bed);
    painter.drawRoundedRect(body, r, r);

    // The sliding accent behind the active half.
    const qreal half = body.width() / 2.0;
    QRectF pill(body.left() + 2 + pos_ * (half - 2), body.top() + 2, half - 2,
                body.height() - 4);
    QColor fill = theme::kAccent;
    fill.setAlphaF(0.16);
    painter.setBrush(fill);
    painter.drawRoundedRect(pill, r - 2, r - 2);

    QFont f = font();
    f.setPixelSize(14);
    f.setWeight(QFont::DemiBold);
    painter.setFont(f);

    painter.setPen(mix(theme::kDim, theme::kInk, 1.0 - pos_));
    painter.drawText(QRectF(body.left(), body.top(), half, body.height()), Qt::AlignCenter,
                     QStringLiteral("Simple"));
    painter.setPen(mix(theme::kDim, theme::kInk, pos_));
    painter.drawText(QRectF(body.left() + half, body.top(), half, body.height()),
                     Qt::AlignCenter, QStringLiteral("Expert"));
}

// -------------------------------------------------------------------- Chip

Chip::Chip(const QString& text, QWidget* parent) : QAbstractButton(parent) {
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(42);
    anim_ = hoverAnimation(this, &hover_, this);
}

QSize Chip::sizeHint() const {
    QFont font = this->font();
    font.setPixelSize(18);
    return {QFontMetrics(font).horizontalAdvance(text()) + 48, 42};
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
    font.setPixelSize(16);
    painter.setFont(font);
    painter.setPen(mix(theme::kDim, theme::kInk, hover_));
    painter.drawText(body, Qt::AlignCenter, text());
}

}  // namespace mimi::ui
