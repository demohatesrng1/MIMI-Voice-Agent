#include "ui/voice_orb.hpp"

#include "ui/theme.hpp"

#include <QBitmap>
#include <QConicalGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QRadialGradient>
#include <QtMath>

#include <algorithm>

namespace mimi::ui {
namespace {

// Live level history, drawn as a ring of spokes around the core. Long enough to
// show the shape of a sentence, short enough that it still feels immediate.
constexpr int kSpokes = 64;

qreal ease(qreal t) { return t * t * (3.0 - 2.0 * t); }

}  // namespace

VoiceOrb::VoiceOrb(QWidget* parent) : QWidget(parent), levels_(kSpokes, 0.0) {
    setAttribute(Qt::WA_TranslucentBackground);

    pulse_ = new QPropertyAnimation(this, "phase", this);
    pulse_->setStartValue(0.0);
    pulse_->setEndValue(1.0);
    pulse_->setDuration(3200);
    pulse_->setLoopCount(-1);
    pulse_->start();

    // Independent of the breath, so the rings never lock into a single rhythm.
    spin_ = new QPropertyAnimation(this, "spin", this);
    spin_->setStartValue(0.0);
    spin_->setEndValue(360.0);
    spin_->setDuration(11000);
    spin_->setLoopCount(-1);
    spin_->start();
}

void VoiceOrb::setPhase(qreal phase) {
    phase_ = phase;
    update();
}

void VoiceOrb::setSpin(qreal spin) {
    angle_ = spin;
    update();
}

void VoiceOrb::setState(int state) {
    const auto next = static_cast<voice::State>(state);
    if (next == state_) return;
    state_ = next;

    switch (state_) {
        case voice::State::Idle:      pulse_->setDuration(3200); spin_->setDuration(11000); break;
        case voice::State::Listening: pulse_->setDuration(1500); spin_->setDuration(5200);  break;
        case voice::State::Thinking:  pulse_->setDuration(850);  spin_->setDuration(1700);  break;
        case voice::State::Speaking:  pulse_->setDuration(1800); spin_->setDuration(7000);  break;
        case voice::State::Paused:    pulse_->setDuration(6000); spin_->setDuration(30000); break;
    }
    if (state_ == voice::State::Paused) {
        level_ = 0.0;
        std::fill(levels_.begin(), levels_.end(), 0.0);
    }
    update();
}

void VoiceOrb::setLevel(float rms) {
    const qreal target = std::clamp(static_cast<qreal>(rms) * 9.0, 0.0, 1.0);
    // Fast attack, slow release: speech should punch, silence should fade.
    level_ += (target - level_) * (target > level_ ? 0.6 : 0.10);

    levels_[cursor_] = level_;
    cursor_ = (cursor_ + 1) % static_cast<int>(levels_.size());
    update();
}

const QPixmap& VoiceOrb::portrait(int diameter) const {
    if (portrait_size_ == diameter && !portrait_cache_.isNull()) return portrait_cache_;

    QPixmap source(QStringLiteral(":/mimi_512.png"));
    if (source.isNull()) {
        portrait_cache_ = QPixmap();
        portrait_size_ = diameter;
        return portrait_cache_;
    }

    // The asset already carries the rounded-rect icon shape and its shadow, so
    // trim to the artwork before re-masking to a circle -- otherwise the corners
    // and shadow show up as a grey halo inside the orb.
    const int inset = static_cast<int>(source.width() * 0.10);
    source = source.copy(inset, inset, source.width() - inset * 2,
                         source.height() - inset * 2);

    const int scale = diameter * 2;  // retina
    QPixmap scaled = source.scaled(scale, scale, Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);

    QPixmap circular(scale, scale);
    circular.fill(Qt::transparent);
    {
        QPainter painter(&circular);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addEllipse(0, 0, scale, scale);
        painter.setClipPath(clip);
        painter.drawPixmap(0, 0, scaled);
    }
    circular.setDevicePixelRatio(2.0);

    portrait_cache_ = circular;
    portrait_size_ = diameter;
    return portrait_cache_;
}

QColor VoiceOrb::accent() const {
    // One hue at four weights. Resting is deep and quiet, hearing you is the
    // full accent, thinking is lighter, speaking goes almost white -- so the
    // state is read from brightness without needing a legend.
    switch (state_) {
        case voice::State::Idle:      return theme::kPinkDeep;
        case voice::State::Listening: return theme::kPink;
        case voice::State::Thinking:  return theme::kPinkSoft;
        case voice::State::Speaking:  return theme::kInk;
        case voice::State::Paused:    return theme::kFaint;
    }
    return theme::kPinkDeep;
}

void VoiceOrb::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPointF centre(width() / 2.0, height() / 2.0);
    const qreal span = std::min(width(), height()) / 2.0 * 0.80;
    const qreal breath = ease(0.5 - 0.5 * std::cos(phase_ * 2.0 * M_PI));
    const QColor colour = accent();

    // --- atmospheric glow ---------------------------------------------------
    const qreal glow = span * (0.92 + 0.06 * breath + 0.14 * level_);
    QRadialGradient halo(centre, glow);
    QColor core_glow = colour;
    core_glow.setAlphaF(0.20 + 0.18 * level_);
    QColor mid_glow = colour;
    mid_glow.setAlphaF(0.07);
    QColor no_glow = colour;
    no_glow.setAlphaF(0.0);
    halo.setColorAt(0.0, core_glow);
    halo.setColorAt(0.45, mid_glow);
    halo.setColorAt(1.0, no_glow);
    painter.setPen(Qt::NoPen);
    painter.setBrush(halo);
    painter.drawEllipse(centre, glow, glow);

    // --- level spokes -------------------------------------------------------
    // A ring of bars carrying the recent history of the microphone. Reading
    // anticlockwise from the top, the newest sample is always at 12 o'clock.
    const qreal inner = span * 0.60;
    const int count = static_cast<int>(levels_.size());
    for (int i = 0; i < count; ++i) {
        const int index = (cursor_ - 1 - i + count * 2) % count;
        const qreal value = levels_[index];
        const qreal angle = (static_cast<qreal>(i) / count) * 2.0 * M_PI - M_PI_2;
        const qreal length = span * (0.05 + 0.30 * value);

        QColor spoke = colour;
        // Fade with age so the ring reads as a trail, not a static equaliser.
        spoke.setAlphaF(0.14 + 0.60 * value * (1.0 - static_cast<qreal>(i) / count));

        QPen pen(spoke, 2.0);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(
            QPointF(centre.x() + std::cos(angle) * inner, centre.y() + std::sin(angle) * inner),
            QPointF(centre.x() + std::cos(angle) * (inner + length),
                    centre.y() + std::sin(angle) * (inner + length)));
    }

    // --- rotating rings -----------------------------------------------------
    const qreal ring = span * 0.52;
    {
        // A conical sweep gives the ring a light source that travels round it.
        QConicalGradient sweep(centre, -angle_);
        QColor bright = colour.lighter(150);
        bright.setAlphaF(0.95);
        QColor dark = colour;
        dark.setAlphaF(0.12);
        sweep.setColorAt(0.00, dark);
        sweep.setColorAt(0.25, bright);
        sweep.setColorAt(0.55, dark);
        sweep.setColorAt(1.00, dark);

        QPen pen(QBrush(sweep), 2.4 + 1.6 * level_);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(centre, ring, ring);
    }

    // Counter-rotating dashed ring, for depth.
    {
        QColor faint = colour;
        faint.setAlphaF(0.30);
        QPen pen(faint, 1.0);
        pen.setDashPattern({1.0, 5.0});
        painter.setPen(pen);
        painter.save();
        painter.translate(centre);
        painter.rotate(angle_ * 0.6);
        painter.drawEllipse(QPointF(0, 0), ring * 1.16, ring * 1.16);
        painter.restore();
    }

    // Thinking: a bright arc chasing the ring, so waiting reads as progress.
    if (state_ == voice::State::Thinking) {
        QPen pen(theme::kInk, 2.8);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        const QRectF box(centre.x() - ring * 1.30, centre.y() - ring * 1.30, ring * 2.60,
                         ring * 2.60);
        painter.drawArc(box, static_cast<int>(-angle_ * 16), 70 * 16);
    }

    // --- core: the character herself ----------------------------------------
    const qreal core = span * 0.44 * (1.0 + 0.04 * breath + 0.06 * level_);
    const int diameter = static_cast<int>(core * 2);
    const QPixmap& face = portrait(diameter);

    if (!face.isNull()) {
        painter.drawPixmap(QPointF(centre.x() - core, centre.y() - core), face);

        // Tint the portrait toward the state colour so the orb still reads as a
        // status light rather than just a picture.
        QColor tint = colour;
        tint.setAlphaF(0.20 + 0.10 * level_);
        painter.setPen(Qt::NoPen);
        painter.setBrush(tint);
        painter.setCompositionMode(QPainter::CompositionMode_Overlay);
        painter.drawEllipse(centre, core, core);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    } else {
        QRadialGradient fill(QPointF(centre.x() - core * 0.25, centre.y() - core * 0.35),
                             core * 1.9);
        fill.setColorAt(0.0, colour.lighter(165));
        fill.setColorAt(1.0, theme::kBg);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawEllipse(centre, core, core);
    }

    // Rim light, so she sits inside the rings rather than on top of them.
    QColor rim = colour.lighter(170);
    rim.setAlphaF(0.75);
    painter.setPen(QPen(rim, 1.6));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(centre, core, core);
}

}  // namespace mimi::ui
