#include "ui/ambient.hpp"

#include "ui/theme.hpp"

#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QTimer>
#include <QtMath>

namespace mimi::ui {

AmbientCanvas::AmbientCanvas(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    // 24 fps is plenty for light that takes a minute to cross the window,
    // and keeps the whole-surface repaint cheap.
    tick_ = new QTimer(this);
    tick_->setInterval(42);
    connect(tick_, &QTimer::timeout, this, [this] {
        phase_ += 42.0 / 60000.0;  // one full cycle per minute
        if (phase_ >= 1.0) phase_ -= 1.0;
        update();
    });
    tick_->start();
}

void AmbientCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const qreal w = width();
    const qreal h = height();
    const qreal t = phase_ * 2.0 * M_PI;

    // Base graphite, slightly lighter at the top: one large, soft light
    // source above the interface, like every product shot ever lit.
    QLinearGradient base(0, 0, 0, h);
    QColor top = theme::kLayer0;
    top.setAlpha(234);
    QColor bottom = theme::kVoid;
    bottom.setAlpha(240);
    base.setColorAt(0.0, top);
    base.setColorAt(1.0, bottom);
    painter.fillRect(rect(), base);

    // Two pools of light drifting in opposite directions. Alphas are single
    // digits: the light should be felt, not seen. The first pool slowly walks
    // the spectrum -- black, with a breath of RGB living in it.
    const QPointF a(w * (0.30 + 0.18 * std::cos(t)), h * (0.10 + 0.08 * std::sin(t * 0.7)));
    QRadialGradient poolA(a, w * 0.75);
    QColor glowA = QColor::fromHsvF(phase_, 0.55, 1.0);
    glowA.setAlpha(16);
    poolA.setColorAt(0.0, glowA);
    glowA.setAlpha(0);
    poolA.setColorAt(1.0, glowA);
    painter.fillRect(rect(), poolA);

    const QPointF b(w * (0.78 - 0.15 * std::sin(t * 0.8)), h * (0.95 - 0.06 * std::cos(t)));
    QRadialGradient poolB(b, w * 0.65);
    QColor glowB = theme::kAccentDeep;
    glowB.setAlpha(22);
    poolB.setColorAt(0.0, glowB);
    glowB.setAlpha(0);
    poolB.setColorAt(1.0, glowB);
    painter.fillRect(rect(), poolB);

    // Vignette, so the frame recedes and the centre carries the content.
    QRadialGradient edge(QPointF(w / 2, h / 2), std::max(w, h) * 0.72);
    edge.setColorAt(0.70, QColor(0, 0, 0, 0));
    edge.setColorAt(1.00, QColor(0, 0, 0, 60));
    painter.fillRect(rect(), edge);
}

}  // namespace mimi::ui
