#include "ui/floating_orb.hpp"

#include "ui/theme.hpp"

#include <QAction>
#include <QApplication>
#include <QConicalGradient>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QRadialGradient>
#include <QScreen>
#include <QtMath>

#include <algorithm>

namespace mimi::ui {
namespace {

constexpr int kDisc = 84;      // the artwork circle
constexpr int kPadding = 16;   // room for the glow, which reaches past the disc
constexpr int kDragSlop = 4;
constexpr int kEdgeMargin = 18;

qreal ease(qreal t) { return t * t * (3.0 - 2.0 * t); }

}  // namespace

FloatingOrb::FloatingOrb(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                   Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(kDisc + kPadding * 2, kDisc + kPadding * 2);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Mimi — click to open, double-click to talk"));

    pulse_ = new QPropertyAnimation(this, "phase", this);
    pulse_->setStartValue(0.0);
    pulse_->setEndValue(1.0);
    pulse_->setDuration(3200);
    pulse_->setLoopCount(-1);
    pulse_->start();

    spin_ = new QPropertyAnimation(this, "spin", this);
    spin_->setStartValue(0.0);
    spin_->setEndValue(360.0);
    spin_->setDuration(9000);
    spin_->setLoopCount(-1);
    spin_->start();

    menu_ = new QMenu(this);
    auto* open = menu_->addAction(QStringLiteral("ウィンドウを開く   Open window"));
    connect(open, &QAction::triggered, this, &FloatingOrb::clicked);
    auto* talk = menu_->addAction(QStringLiteral("話しかける   Talk now"));
    connect(talk, &QAction::triggered, this, &FloatingOrb::doubleClicked);
    menu_->addSeparator();
    auto* mute = menu_->addAction(QStringLiteral("マイクをオフ   Mute microphone"));
    mute->setCheckable(true);
    connect(mute, &QAction::toggled, this, [this](bool on) {
        muted_ = on;
        Q_EMIT muteRequested(on);
        update();
    });
    menu_->addSeparator();
    auto* quit = menu_->addAction(QStringLiteral("終了   Quit Mimi"));
    connect(quit, &QAction::triggered, this, &FloatingOrb::quitRequested);
}

void FloatingOrb::setPhase(qreal phase) {
    phase_ = phase;
    update();
}

void FloatingOrb::setSpin(qreal spin) {
    angle_ = spin;
    update();
}

void FloatingOrb::setState(int state) {
    const auto next = static_cast<voice::State>(state);
    if (next == state_) return;
    state_ = next;
    switch (state_) {
        case voice::State::Idle:      pulse_->setDuration(3200); spin_->setDuration(5500);  break;
        case voice::State::Listening: pulse_->setDuration(1400); spin_->setDuration(2800);  break;
        case voice::State::Thinking:  pulse_->setDuration(800);  spin_->setDuration(1000);  break;
        case voice::State::Speaking:  pulse_->setDuration(1700); spin_->setDuration(3500);  break;
        case voice::State::Paused:    pulse_->setDuration(6000); spin_->setDuration(15000); break;
    }
    if (state_ == voice::State::Paused) level_ = 0.0;
    update();
}

void FloatingOrb::setLevel(float rms) {
    const qreal target = std::clamp(static_cast<qreal>(rms) * 9.0, 0.0, 1.0);
    level_ += (target - level_) * (target > level_ ? 0.6 : 0.10);
    update();
}

QColor FloatingOrb::accent() const {
    if (muted_) return theme::kError;
    switch (state_) {
        case voice::State::Idle:      return theme::kAccentDeep;
        case voice::State::Listening: return theme::kAccent;
        case voice::State::Thinking:  return theme::kAccentSoft;
        case voice::State::Speaking:  return theme::kInk;
        case voice::State::Paused:    return theme::kFaint;
    }
    return theme::kAccentDeep;
}

const QPixmap& FloatingOrb::portrait() const {
    QPixmap& cache = muted_ ? muted_cache_ : portrait_cache_;
    if (!cache.isNull()) return cache;

    // A clean full-bleed crop straight from the master art, her glow ring at
    // the edge of the disc.
    QPixmap source(QStringLiteral(":/mimi_face.png"));
    if (source.isNull()) return cache;

    const int scale = kDisc * 2;  // retina
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

        if (muted_) {
            // Drain the colour rather than dimming: muted should look switched
            // off, and a dimmed portrait just looks like a rendering fault.
            QImage grey = circular.toImage().convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < grey.height(); ++y) {
                auto* line = reinterpret_cast<QRgb*>(grey.scanLine(y));
                for (int x = 0; x < grey.width(); ++x) {
                    const QRgb p = line[x];
                    const int v = qGray(p);
                    line[x] = qRgba(v, v, v, qAlpha(p));
                }
            }
            painter.end();
            circular = QPixmap::fromImage(grey);
        }
    }
    circular.setDevicePixelRatio(2.0);
    cache = circular;
    return cache;
}

void FloatingOrb::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPointF centre(width() / 2.0, height() / 2.0);
    const qreal radius = kDisc / 2.0;
    const qreal breath = ease(0.5 - 0.5 * std::cos(phase_ * 2.0 * M_PI));
    const QColor colour = accent();

    // --- glow, reaching into the padding -----------------------------------
    const qreal glow = radius * (1.30 + 0.10 * breath + 0.22 * level_);
    QRadialGradient halo(centre, glow);
    QColor inner = colour;
    inner.setAlphaF((hovered_ ? 0.34 : 0.24) + 0.24 * level_);
    QColor outer = colour;
    outer.setAlphaF(0.0);
    halo.setColorAt(radius / glow * 0.92, inner);
    halo.setColorAt(1.0, outer);
    painter.setPen(Qt::NoPen);
    painter.setBrush(halo);
    painter.drawEllipse(centre, glow, glow);

    // --- her, filling the disc ---------------------------------------------
    const QPixmap& face = portrait();
    if (!face.isNull()) {
        painter.drawPixmap(QPointF(centre.x() - radius, centre.y() - radius), face);
    } else {
        painter.setBrush(theme::kLayer2);
        painter.drawEllipse(centre, radius, radius);
    }

    // --- state ring, right on the edge -------------------------------------
    // Everything the orb has to communicate lives here, so nothing is drawn
    // over her face.
    {
        QConicalGradient sweep(centre, -angle_);
        if (muted_ || state_ == voice::State::Paused) {
            // Muted stays monochrome: the spectrum is a sign of life.
            QColor bright = colour.lighter(150);
            bright.setAlphaF(1.0);
            QColor dim = colour;
            dim.setAlphaF(0.35);
            sweep.setColorAt(0.00, dim);
            sweep.setColorAt(0.22, bright);
            sweep.setColorAt(0.55, dim);
            sweep.setColorAt(1.00, dim);
        } else {
            // The full spectrum riding the edge, spinning with the ring.
            for (int i = 0; i <= 6; ++i) {
                QColor hue = QColor::fromHsvF(std::fmod(i / 6.0, 1.0), 0.72, 1.0);
                sweep.setColorAt(i / 6.0, hue);
            }
        }

        QPen ring(QBrush(sweep), 2.6 + 2.4 * level_);
        painter.setPen(ring);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(centre, radius - 1.0, radius - 1.0);
    }

    // Thinking: a bright arc chasing the ring, so the wait reads as progress.
    if (state_ == voice::State::Thinking) {
        QPen arc(theme::kInk, 2.6);
        arc.setCapStyle(Qt::RoundCap);
        painter.setPen(arc);
        const qreal r = radius + 4.0;
        painter.drawArc(QRectF(centre.x() - r, centre.y() - r, r * 2, r * 2),
                        static_cast<int>(-angle_ * 16), 80 * 16);
    }

    // Listening: ticks around the outside that rise with the microphone, so you
    // can see she is hearing you without opening anything.
    if (level_ > 0.02 && state_ != voice::State::Paused) {
        QColor tick = colour;
        tick.setAlphaF(0.30 + 0.60 * level_);
        QPen pen(tick, 1.8);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        constexpr int kTicks = 36;
        for (int i = 0; i < kTicks; ++i) {
            const qreal a = (static_cast<qreal>(i) / kTicks) * 2.0 * M_PI;
            // Vary the length around the ring so it reads as a meter, not a
            // uniform halo.
            const qreal wobble = 0.55 + 0.45 * std::sin(a * 3.0 + phase_ * 6.28);
            const qreal from = radius + 3.0;
            const qreal to = from + 6.0 * level_ * wobble;
            painter.drawLine(
                QPointF(centre.x() + std::cos(a) * from, centre.y() + std::sin(a) * from),
                QPointF(centre.x() + std::cos(a) * to, centre.y() + std::sin(a) * to));
        }
    }

    if (muted_) {
        QPen slash(QColor(theme::kError.red(), theme::kError.green(), theme::kError.blue(), 235), 3.0);
        slash.setCapStyle(Qt::RoundCap);
        painter.setPen(slash);
        const qreal d = radius * 0.62;
        painter.drawLine(QPointF(centre.x() - d, centre.y() - d),
                         QPointF(centre.x() + d, centre.y() + d));
    }
}

void FloatingOrb::moveToDefaultCorner() {
    const QScreen* screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) return;
    const QRect area = screen->availableGeometry();
    move(area.right() - width() - kEdgeMargin, area.bottom() - height() - kEdgeMargin * 4);
}

void FloatingOrb::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    dragging_ = true;
    moved_beyond_threshold_ = false;
    press_offset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
}

void FloatingOrb::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    const QPoint target = event->globalPosition().toPoint() - press_offset_;
    if ((target - pos()).manhattanLength() > kDragSlop) moved_beyond_threshold_ = true;
    move(target);
}

void FloatingOrb::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    dragging_ = false;
    if (!moved_beyond_threshold_) {
        Q_EMIT clicked();
        return;
    }
    snapToEdge();
}

void FloatingOrb::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) Q_EMIT doubleClicked();
}

void FloatingOrb::contextMenuEvent(QContextMenuEvent* event) {
    menu_->exec(event->globalPos());
}

void FloatingOrb::enterEvent(QEnterEvent*) {
    hovered_ = true;
    update();
}

void FloatingOrb::leaveEvent(QEvent*) {
    hovered_ = false;
    update();
}

void FloatingOrb::snapToEdge() {
    const QScreen* screen = QGuiApplication::screenAt(frameGeometry().center());
    if (screen == nullptr) screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) return;

    const QRect area = screen->availableGeometry();
    QPoint target = pos();
    const int distance_left = target.x() - area.left();
    const int distance_right = area.right() - (target.x() + width());
    target.setX(distance_left <= distance_right ? area.left() + kEdgeMargin
                                                : area.right() - width() - kEdgeMargin);
    target.setY(std::clamp(target.y(), area.top() + kEdgeMargin,
                           area.bottom() - height() - kEdgeMargin));
    move(target);
}

}  // namespace mimi::ui
