#include "ui/ambient.hpp"

#include "ui/theme.hpp"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QTimer>
#include <QtMath>

#include <algorithm>

namespace mimi::ui {
namespace {

// 24 fps: plenty for light that takes a minute to cross the window, and cheap
// enough to repaint the whole surface with a particle field on top.
constexpr int kTickMs = 42;
// How fast a signature weighs in or out. One smoothing step per tick; ~0.06
// lands a fade in a little over half a second, which reads as "settling" rather
// than switching.
constexpr qreal kFade = 0.06;
constexpr int kMotes = 54;

// Where the wave breathes from and the memory collapses toward: roughly where
// the orb sits on the home surface. Normalised across the canvas.
constexpr qreal kHeartX = 0.50;
constexpr qreal kHeartY = 0.44;

qreal frand(qreal lo, qreal hi) {
    return lo + (hi - lo) * QRandomGenerator::global()->generateDouble();
}

// A soft round dot with an alpha falloff, rendered once at 2x for retina. Every
// mote is this same sprite drawn at a size and opacity -- one blit each, versus
// a fresh radial gradient per particle per frame.
QPixmap buildMoteSprite() {
    constexpr int s = 32;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QRadialGradient g(s / 2.0, s / 2.0, s / 2.0);
    QColor c = theme::kAccentSoft;
    c.setAlphaF(1.0);
    g.setColorAt(0.0, c);
    c.setAlphaF(0.0);
    g.setColorAt(1.0, c);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(0, 0, s, s);
    return pm;
}

QColor desaturate(QColor c, qreal amount) {
    const qreal grey = 0.30 * c.redF() + 0.59 * c.greenF() + 0.11 * c.blueF();
    return QColor::fromRgbF(c.redF() + (grey - c.redF()) * amount,
                            c.greenF() + (grey - c.greenF()) * amount,
                            c.blueF() + (grey - c.blueF()) * amount);
}

}  // namespace

AmbientCanvas::AmbientCanvas(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    moteSprite_ = buildMoteSprite();

    motes_.reserve(kMotes);
    for (int i = 0; i < kMotes; ++i)
        motes_.push_back({frand(0, 1), frand(0, 1), frand(-0.0006, 0.0006),
                          frand(-0.0006, 0.0006), frand(0, 1)});

    tick_ = new QTimer(this);
    tick_->setInterval(kTickMs);
    connect(tick_, &QTimer::timeout, this, [this] { step(); });
    tick_->start();
}

void AmbientCanvas::setPresence(Presence presence) {
    presence_ = presence;
}

void AmbientCanvas::showEvent(QShowEvent*) {
    if (!tick_->isActive()) tick_->start();
}

void AmbientCanvas::hideEvent(QHideEvent*) { tick_->stop(); }

void AmbientCanvas::setLevel(float rms) {
    const qreal target = std::clamp(static_cast<qreal>(rms) * 7.0, 0.0, 1.0);
    // Fast attack, slow release, so speech lifts the room and silence lets it
    // settle rather than flicker.
    level_ += (target - level_) * (target > level_ ? 0.5 : 0.08);
}

// ------------------------------------------------------------------ the clock

void AmbientCanvas::step() {
    phase_ += kTickMs / 60000.0;        // one drift cycle per minute
    if (phase_ >= 1.0) phase_ -= 1.0;
    flow_ += kTickMs / 6200.0;          // one flow cycle per ~6s
    if (flow_ >= 1.0) flow_ -= 1.0;

    // Weigh each signature toward the presence that owns it.
    qreal want[SigCount] = {0, 0, 0, 0};
    switch (presence_) {
        case Presence::Listening:   want[SigWave] = 1;     break;
        case Presence::Thinking:    want[SigParticle] = 1; break;
        case Presence::Speaking:    want[SigLine] = 1;     break;
        case Presence::Remembering: want[SigConverge] = 1; break;
        case Presence::Observing:
        case Presence::Muted:       break;  // resting drift alone
    }
    for (int i = 0; i < SigCount; ++i)
        weight_[i] += (want[i] - weight_[i]) * kFade;

    const qreal muteTarget = presence_ == Presence::Muted ? 1.0 : 0.0;
    mute_ += (muteTarget - mute_) * kFade;

    // Advance the particle field. Thinking makes it wander; Remembering pulls
    // it inward; at rest it barely breathes. Both forces are weight-scaled so
    // the field is calm the instant neither signature is active.
    const qreal drift = weight_[SigParticle];
    const qreal pull = weight_[SigConverge];
    if (drift + pull > 0.01) {
        for (Mote& m : motes_) {
            m.vx += frand(-0.00018, 0.00018) * drift;
            m.vy += frand(-0.00018, 0.00018) * drift;
            const qreal dx = kHeartX - m.x;
            const qreal dy = kHeartY - m.y;
            m.vx += dx * 0.010 * pull;
            m.vy += dy * 0.010 * pull;
            m.vx *= 0.94;  // drag, so the field never runs away
            m.vy *= 0.94;
            m.x += m.vx;
            m.y += m.vy;
            // Filed away: a mote that reaches her respawns at the edge, so the
            // field streams inward continuously instead of collapsing to a dot.
            if (pull > 0.3 && dx * dx + dy * dy < 0.0016) {
                const bool horizontal = QRandomGenerator::global()->bounded(2) == 0;
                m.x = horizontal ? frand(0, 1) : (QRandomGenerator::global()->bounded(2) ? 1.0 : 0.0);
                m.y = horizontal ? (QRandomGenerator::global()->bounded(2) ? 1.0 : 0.0) : frand(0, 1);
                m.vx = m.vy = 0;
            }
            if (m.x < -0.05) m.x += 1.1;
            if (m.x > 1.05) m.x -= 1.1;
            if (m.y < -0.05) m.y += 1.1;
            if (m.y > 1.05) m.y -= 1.1;
        }
    }

    update();
}

void AmbientCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal w = width();
    const qreal h = height();
    const qreal t = phase_ * 2.0 * M_PI;

    // Base graphite, lighter at the top: one large soft light source above the
    // interface, like every product shot ever lit.
    QLinearGradient base(0, 0, 0, h);
    QColor top = theme::kLayer0;
    top.setAlpha(234);
    QColor bottom = theme::kVoid;
    bottom.setAlpha(240);
    base.setColorAt(0.0, top);
    base.setColorAt(1.0, bottom);
    painter.fillRect(rect(), base);

    // Two pools of resting light drifting in opposite directions. Alphas are
    // single digits: felt, not seen. Muting dims and stills them further.
    const qreal restAlpha = 1.0 - 0.55 * mute_;
    const QPointF a(w * (0.30 + 0.18 * std::cos(t)), h * (0.10 + 0.08 * std::sin(t * 0.7)));
    QRadialGradient poolA(a, w * 0.75);
    QColor glowA = QColor::fromHsvF(phase_, 0.55, 1.0);
    glowA = desaturate(glowA, mute_);
    glowA.setAlphaF(0.063 * restAlpha);
    poolA.setColorAt(0.0, glowA);
    glowA.setAlpha(0);
    poolA.setColorAt(1.0, glowA);
    painter.fillRect(rect(), poolA);

    const QPointF b(w * (0.78 - 0.15 * std::sin(t * 0.8)), h * (0.95 - 0.06 * std::cos(t)));
    QRadialGradient poolB(b, w * 0.65);
    QColor glowB = desaturate(theme::kAccentDeep, mute_);
    glowB.setAlphaF(0.086 * restAlpha);
    poolB.setColorAt(0.0, glowB);
    glowB.setAlpha(0);
    poolB.setColorAt(1.0, glowB);
    painter.fillRect(rect(), poolB);

    // The active signatures, each scaled by its own eased weight so a handover
    // shows both for an instant and neither ever pops.
    const QPointF heart(w * kHeartX, h * kHeartY);
    if (weight_[SigWave] > 0.01) paintWave(painter, heart, w, h, weight_[SigWave]);
    if (weight_[SigParticle] + weight_[SigConverge] > 0.01)
        paintParticles(painter, w, h, weight_[SigParticle], weight_[SigConverge]);
    if (weight_[SigLine] > 0.01) paintLines(painter, w, h, weight_[SigLine]);

    // Vignette, so the frame recedes and the centre carries the content.
    QRadialGradient edge(QPointF(w / 2, h / 2), std::max(w, h) * 0.72);
    edge.setColorAt(0.70, QColor(0, 0, 0, 0));
    edge.setColorAt(1.00, QColor(0, 0, 0, 60));
    painter.fillRect(rect(), edge);
}

// --- Listening: a slow blue wave breathing out from her -----------------------
void AmbientCanvas::paintWave(QPainter& painter, const QPointF& heart, qreal w, qreal h,
                              qreal weight) {
    painter.setBrush(Qt::NoBrush);
    const qreal maxR = std::hypot(w, h) * 0.62;
    constexpr int rings = 4;
    for (int i = 0; i < rings; ++i) {
        // Each ring is a life 0..1; born at the heart, dying at the edge.
        qreal life = std::fmod(flow_ + static_cast<qreal>(i) / rings, 1.0);
        const qreal r = life * maxR;
        // Brightest in the middle of its travel, gone at both ends.
        const qreal envelope = std::sin(life * M_PI);
        QColor c = theme::kAccent;
        c.setAlphaF(0.10 * weight * envelope * (0.55 + 0.6 * level_));
        QPen pen(c, 1.4 + 1.6 * level_);
        painter.setPen(pen);
        painter.drawEllipse(heart, r, r);
    }
}

// --- Thinking / Remembering: a field of motes ---------------------------------
void AmbientCanvas::paintParticles(QPainter& painter, qreal w, qreal h, qreal drift,
                                   qreal pull) {
    const qreal weight = std::clamp(drift + pull, 0.0, 1.0);
    painter.setPen(Qt::NoPen);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const qreal twinkle = flow_ * 2.0 * M_PI;
    for (const Mote& m : motes_) {
        const qreal a = 0.5 + 0.5 * std::sin(twinkle + m.seed * 6.283);
        const qreal alpha = 0.05 + 0.22 * weight * a;
        const qreal d = (3.3 + 4.5 * a);  // sprite diameter on screen
        painter.setOpacity(alpha);
        painter.drawPixmap(QRectF(m.x * w - d / 2, m.y * h - d / 2, d, d), moteSprite_,
                           QRectF(moteSprite_.rect()));
    }
    painter.setOpacity(1.0);
}

// --- Responding: soft flowing lines -------------------------------------------
void AmbientCanvas::paintLines(QPainter& painter, qreal w, qreal h, qreal weight) {
    painter.setBrush(Qt::NoBrush);
    constexpr int lines = 5;
    const qreal scroll = flow_ * 2.0 * M_PI;
    for (int i = 0; i < lines; ++i) {
        const qreal baseY = h * (0.18 + 0.64 * i / (lines - 1));
        const qreal amp = h * 0.05 * (0.7 + 0.5 * level_);
        QPainterPath path;
        for (int x = 0; x <= 48; ++x) {
            const qreal fx = static_cast<qreal>(x) / 48.0;
            const qreal y = baseY + amp * std::sin(fx * 3.0 * M_PI - scroll - i * 0.6);
            const QPointF p(fx * w, y);
            if (x == 0) path.moveTo(p);
            else path.lineTo(p);
        }
        QColor c = theme::kAccentSoft;
        c.setAlphaF(0.05 * weight);
        painter.setPen(QPen(c, 1.2));
        painter.drawPath(path);
    }
}

}  // namespace mimi::ui
