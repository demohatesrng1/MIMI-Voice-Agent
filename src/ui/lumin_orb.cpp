#include "ui/lumin_orb.hpp"

#include "ui/theme.hpp"

#include <QDateTime>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTimer>
#include <QtMath>

#include <random>

namespace mimi::ui {
namespace {

// The breath. One period for the aura, the status dot and everything else that
// signals she is present, so the interface has a single heartbeat.
constexpr double kBreathMs = 3000.0;
// How long the gesture takes. Long enough not to fire on a click, short enough
// that you do not wonder whether it is working.
constexpr double kHoldMs = 1500.0;
constexpr int kMotes = 44;

struct Palette {
    QColor accent;
    QColor core;
    QColor deep;
    const char* word;
};

// Only the orb and what borrows from it use these. The ground stays #0A0A0F at
// every hour of the day -- the light in the room changes, the room does not.
const Palette& palette_for(LuminOrb::Light light) {
    static const Palette morning{QColor(0xF5, 0xA5, 0x24), QColor(0xFF, 0xE3, 0xB0),
                                 QColor(0xA9, 0x64, 0x0A), "Good morning"};
    static const Palette afternoon{QColor(0x22, 0xD3, 0xEE), QColor(0xCF, 0xFA, 0xFE),
                                   QColor(0x0B, 0x7C, 0x96), "Good afternoon"};
    static const Palette night{QColor(0xA7, 0x8B, 0xFA), QColor(0xED, 0xE9, 0xFE),
                               QColor(0x6D, 0x46, 0xD9), "Good evening"};
    Q_UNUSED(morning);
    switch (light) {
        case LuminOrb::Light::Morning:   return morning;
        case LuminOrb::Light::Afternoon: return afternoon;
        default:                         return night;
    }
}

QColor alpha(QColor colour, double a) {
    colour.setAlphaF(std::clamp(a, 0.0, 1.0));
    return colour;
}

}  // namespace

LuminOrb::LuminOrb(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(minimumSizeHint());

    // Seven motes, each on its own orbit. Identical orbits read as a machine
    // part; the variation is what makes it look alive.
    std::mt19937 rng{20260809};
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    motes_.reserve(kMotes);
    for (int i = 0; i < kMotes; ++i) {
        Mote mote;
        mote.angle = unit(rng) * 2 * M_PI;
        // A field, not a ring: the motes fill the space around her at every
        // radius, so the volume reads as dense rather than as one orbit.
        mote.base = 1.05 + unit(rng) * 1.35;
        mote.radius = mote.base;
        mote.speed = (0.14 + unit(rng) * 0.5) * (unit(rng) < 0.25 ? -1.0 : 1.0);
        mote.tilt = 0.18 + unit(rng) * 0.72;
        mote.phase = unit(rng) * 2 * M_PI;
        motes_.append(mote);
    }

    elapsed_.start();
    lastMs_ = elapsed_.elapsed();
    clock_ = new QTimer(this);
    clock_->setInterval(16);
    connect(clock_, &QTimer::timeout, this, &LuminOrb::tick);
    clock_->start();

    lastHour_ = QDateTime::currentDateTime().time().hour();
}

LuminOrb::Light LuminOrb::resolved() const {
    if (light_ != Light::Auto) return light_;
    const int hour = QDateTime::currentDateTime().time().hour();
    if (hour >= 5 && hour < 12) return Light::Morning;
    if (hour >= 12 && hour < 18) return Light::Afternoon;
    return Light::Night;
}

QColor LuminOrb::accent() const { return palette_for(resolved()).accent; }
QColor LuminOrb::accentCore() const { return palette_for(resolved()).core; }
QString LuminOrb::greetingWord() const {
    return QString::fromUtf8(palette_for(resolved()).word);
}

void LuminOrb::setLight(Light light) {
    if (light_ == light) return;
    light_ = light;
    Q_EMIT lightChanged();
    update();
}

void LuminOrb::setPresence(Presence presence) {
    presence_ = presence;
    update();
}

// Cyan is attention, violet is thought, and she crosses between them rather
// than switching -- the transit is what makes the state legible from across the
// room without a label.
QColor LuminOrb::stateAccent() const {
    const QColor cyan = theme::kAccent;
    const QColor violet = theme::kLive;
    const double t = std::clamp(think_, 0.0, 1.0);
    return QColor::fromRgbF(cyan.redF() + (violet.redF() - cyan.redF()) * t,
                            cyan.greenF() + (violet.greenF() - cyan.greenF()) * t,
                            cyan.blueF() + (violet.blueF() - cyan.blueF()) * t);
}

void LuminOrb::setLevel(float rms) {
    // Fast up, slow down, exactly as the avatar does it: the attack of a
    // syllable is the part worth showing.
    const double v = std::clamp(static_cast<double>(rms), 0.0, 1.0);
    level_ = v > level_ ? level_ * 0.35 + v * 0.65 : level_ * 0.88 + v * 0.12;
}

void LuminOrb::tick() {
    const qint64 now = elapsed_.elapsed();
    const double dt = std::min((now - lastMs_) / 1000.0, 0.05);
    lastMs_ = now;

    phase_ = std::fmod(now / kBreathMs, 1.0);

    // Dusk arrives while the app is open; nobody relaunches for it.
    if (light_ == Light::Auto) {
        const int hour = QDateTime::currentDateTime().time().hour();
        if (hour != lastHour_) {
            lastHour_ = hour;
            Q_EMIT lightChanged();
        }
    }

    const double inhaleTarget = inhaling_ || holding_ ? 1.0 : 0.0;
    const double rate = inhaleTarget > inhale_ ? 7.0 : 2.6;
    inhale_ += (inhaleTarget - inhale_) * (1.0 - std::exp(-rate * dt));

    for (Mote& mote : motes_) {
        const double target = mote.base * (1.0 - 0.93 * inhale_);
        mote.radius += (target - mote.radius) * (1.0 - std::exp(-4.0 * dt));
        mote.angle += mote.speed * (1.0 + 4.2 * inhale_) * dt;
    }

    if (holding_) {
        hold_ = std::min(hold_ + dt * 1000.0 / kHoldMs, 1.0);
        if (hold_ >= 1.0) {
            holding_ = false;
            hold_ = 0.0;
            shake_ = 1.0;
            Q_EMIT held();
        }
    }
    if (shake_ > 0.0) shake_ = std::max(0.0, shake_ - dt * 2.4);

    // Toward violet while she works, back to cyan when she is with you.
    const double thinkTarget =
        (presence_ == Presence::Thinking || presence_ == Presence::Remembering) ? 1.0 : 0.0;
    think_ += (thinkTarget - think_) * (1.0 - std::exp(-3.0 * dt));

    // She floats. A figure that holds one altitude reads as a rendered sphere;
    // a slow bob reads as something suspended in front of you.
    bob_ = std::sin(now / 2600.0 * 2 * M_PI) * 0.5 + 0.5;

    update();
}

void LuminOrb::enterEvent(QEnterEvent* event) {
    inhaling_ = true;
    QWidget::enterEvent(event);
}

void LuminOrb::leaveEvent(QEvent* event) {
    inhaling_ = false;
    holding_ = false;
    hold_ = 0.0;
    QWidget::leaveEvent(event);
}

void LuminOrb::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    holding_ = true;
    hold_ = 0.0;
}

void LuminOrb::mouseReleaseEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    holding_ = false;
    hold_ = 0.0;
}

void LuminOrb::keyPressEvent(QKeyEvent* event) {
    // The hold must never be the only way in.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
        shake_ = 1.0;
        Q_EMIT held();
        return;
    }
    QWidget::keyPressEvent(event);
}

void LuminOrb::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // The hour still sets the core's warmth; the *state* sets the aura, the
    // rings and the field. One says when it is, the other says what she is
    // doing, and they are different questions.
    const QColor accent = stateAccent();
    const QColor core_colour = palette_for(resolved()).core;

    const double span = std::min(width(), height());
    // She floats, and the whole field floats with her.
    QPointF centre(width() / 2.0, height() / 2.0 + (bob_ - 0.5) * span * 0.035);

    if (shake_ > 0.0) {
        const double amp = shake_ * shake_ * 5.0;
        centre += QPointF(std::sin(lastMs_ * 0.06) * amp, std::cos(lastMs_ * 0.08) * amp * 0.6);
    }

    const double core = span * 0.17 * (1.0 - 0.06 * inhale_) * (1.0 + 0.06 * level_) *
                        (1.0 + 0.06 * shake_);

    // --- the field ---------------------------------------------------------
    // Drawn behind everything: motes at every radius rather than on one orbit,
    // so the space around her reads as volume instead of as a hoop.
    for (const Mote& mote : motes_) {
        const double r = core * mote.radius;
        const QPointF at(centre.x() + std::cos(mote.angle) * r,
                         centre.y() + std::sin(mote.angle + mote.phase) * r * mote.tilt);
        const double depth = std::clamp((mote.base - 1.0) / 1.35, 0.0, 1.0);
        QColor dot = depth > 0.55 ? theme::kLive : accent;
        painter.setPen(Qt::NoPen);
        painter.setBrush(alpha(dot, 0.20 + 0.62 * (1.0 - depth)));
        const double size = 0.9 + 1.5 * (1.0 - depth);
        painter.drawEllipse(at, size, size);
    }

    // --- data rings --------------------------------------------------------
    // Two counter-rotating arcs with ticks. They are the only thing on screen
    // that reads as instrumentation, which is what keeps her from looking like
    // a lava lamp.
    const double spin = lastMs_ / 1000.0;
    for (int ring = 0; ring < 2; ++ring) {
        const double radius = core * (ring == 0 ? 1.95 : 2.55);
        const double dir = ring == 0 ? 1.0 : -0.62;
        painter.setBrush(Qt::NoBrush);

        QPen faint(alpha(accent, 0.13));
        faint.setWidthF(1.0);
        painter.setPen(faint);
        painter.drawEllipse(centre, radius, radius);

        // Four arc segments, so the ring reads as rotating.
        QPen arc(alpha(accent, ring == 0 ? 0.55 : 0.34));
        arc.setWidthF(ring == 0 ? 1.8 : 1.2);
        arc.setCapStyle(Qt::RoundCap);
        painter.setPen(arc);
        const QRectF box(centre.x() - radius, centre.y() - radius, radius * 2, radius * 2);
        for (int seg = 0; seg < 4; ++seg) {
            const double start = spin * dir * 26.0 + seg * 90.0;
            painter.drawArc(box, static_cast<int>(start * 16), static_cast<int>(34 * 16));
        }

        // Ticks on the outer ring only -- both would be noise.
        if (ring == 1) {
            painter.setPen(Qt::NoPen);
            for (int tick = 0; tick < 24; ++tick) {
                const double a = spin * dir * 0.45 + tick * (2 * M_PI / 24);
                const QPointF at(centre.x() + std::cos(a) * radius,
                                 centre.y() + std::sin(a) * radius);
                painter.setBrush(alpha(accent, tick % 6 == 0 ? 0.5 : 0.18));
                painter.drawEllipse(at, tick % 6 == 0 ? 1.5 : 0.9, tick % 6 == 0 ? 1.5 : 0.9);
            }
        }
    }

    // --- aura: two shells on one 3s clock, half a period apart -------------
    for (int shell = 0; shell < 2; ++shell) {
        const double t = std::fmod(phase_ + shell * 0.5, 1.0);
        const double radius = core * (1.18 + t * 0.9);
        const double fade = t < 0.35 ? t / 0.35 : 1.0 - (t - 0.35) / 0.65;
        QPen pen(alpha(accent, 0.45 * std::max(0.0, fade)));
        pen.setWidthF(1.4);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(centre, radius, radius);
    }

    // --- bloom, clamped to the widget so it cannot clip to a square --------
    const double reach = std::min(core * 3.4, span * 0.5);
    QRadialGradient bloom(centre, reach);
    bloom.setColorAt(0.0, alpha(accent, 0.34));
    bloom.setColorAt(0.40, alpha(accent, 0.10));
    bloom.setColorAt(1.0, alpha(accent, 0.0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(bloom);
    painter.drawEllipse(centre, reach, reach);

    // --- the core ----------------------------------------------------------
    QRadialGradient body(QPointF(centre.x() - core * 0.28, centre.y() - core * 0.34),
                         core * 1.7);
    body.setColorAt(0.0, core_colour);
    body.setColorAt(0.38, accent);
    body.setColorAt(1.0, alpha(accent, 0.55).darker(260));
    painter.setBrush(body);
    painter.drawEllipse(centre, core, core);

    QRadialGradient shade(QPointF(centre.x() + core * 0.34, centre.y() + core * 0.42),
                          core * 1.25);
    shade.setColorAt(0.0, alpha(QColor(0, 0, 0), 0.30));
    shade.setColorAt(1.0, alpha(QColor(0, 0, 0), 0.0));
    painter.setBrush(shade);
    painter.drawEllipse(centre, core, core);

    // --- the hold ring -----------------------------------------------------
    if (hold_ > 0.0) {
        const double radius = core * 1.5;
        QPen track(alpha(core_colour, 0.14));
        track.setWidthF(1.6);
        painter.setPen(track);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(centre, radius, radius);

        QPen arc(core_colour);
        arc.setWidthF(2.0);
        arc.setCapStyle(Qt::RoundCap);
        painter.setPen(arc);
        const QRectF box(centre.x() - radius, centre.y() - radius, radius * 2, radius * 2);
        painter.drawArc(box, 90 * 16, -static_cast<int>(hold_ * 360 * 16));
    }
}

}  // namespace mimi::ui
