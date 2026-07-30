#include "ui/live_thinking.hpp"

#include "ui/theme.hpp"

#include <QPainter>
#include <QTimer>
#include <QtMath>

#include <array>

namespace mimi::ui {
namespace {

// The pipeline, in the order she works it. Kept short: four beats read as
// deliberate; a longer list reads as a progress bar padded for show.
constexpr std::array<const char*, 4> kStages{
    "Understanding request",
    "Finding context",
    "Building answer",
    "Cross-checking",
};

constexpr int kTickMs = 40;
constexpr int kRowH = 30;
constexpr int kLabelW = 190;
constexpr qreal kFillPerTick = 0.055;  // ~0.7s a stage

}  // namespace

LiveThinking::LiveThinking(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    tick_ = new QTimer(this);
    tick_->setInterval(kTickMs);
    connect(tick_, &QTimer::timeout, this, [this] {
        phase_ += kTickMs / 1100.0;
        if (phase_ >= 1.0) phase_ -= 1.0;
        if (!done_) {
            fill_ += kFillPerTick;
            if (fill_ >= 1.0) {
                if (stage_ < static_cast<int>(kStages.size()) - 1) {
                    ++stage_;
                    fill_ = 0.0;
                } else {
                    fill_ = 1.0;  // hold the last stage, breathing, until finish()
                }
            }
        }
        update();
    });
}

QSize LiveThinking::sizeHint() const {
    return {360, static_cast<int>(kStages.size()) * kRowH};
}

void LiveThinking::start() {
    stage_ = 0;
    fill_ = 0.0;
    done_ = false;
    tick_->start();
    update();
}

void LiveThinking::finish() {
    done_ = true;
    stage_ = static_cast<int>(kStages.size()) - 1;
    fill_ = 1.0;
    tick_->stop();
    update();
}

void LiveThinking::stop() { tick_->stop(); }

void LiveThinking::hideEvent(QHideEvent*) { tick_->stop(); }

void LiveThinking::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int count = static_cast<int>(kStages.size());
    const qreal barX = kLabelW;
    const qreal barW = width() - kLabelW - 8;

    QFont label = font();
    label.setPixelSize(15);

    for (int i = 0; i < count; ++i) {
        const qreal cy = i * kRowH + kRowH / 2.0;
        const bool complete = done_ || i < stage_;
        const bool active = !done_ && i == stage_;
        const qreal frac = complete ? 1.0 : (active ? fill_ : 0.0);

        // The stage's state dot.
        QColor dot = active ? theme::kAccent : complete ? theme::kAccentSoft : theme::kFaint;
        if (active) {
            // Pulse the active dot so the eye knows where the work is.
            dot.setAlphaF(0.55 + 0.45 * (0.5 - 0.5 * std::cos(phase_ * 2.0 * M_PI)));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(dot);
        painter.drawEllipse(QPointF(4, cy), 3.0, 3.0);

        // The stage label.
        painter.setFont(label);
        painter.setPen(active || complete ? theme::kInk : theme::kFaint);
        painter.drawText(QRectF(16, cy - kRowH / 2.0, kLabelW - 24, kRowH),
                         Qt::AlignVCenter | Qt::AlignLeft, QString::fromUtf8(kStages[i]));

        // The track and its fill.
        const QRectF track(barX, cy - 1.5, barW, 3.0);
        QColor bed = theme::kFaint;
        bed.setAlphaF(0.26);
        painter.setBrush(bed);
        painter.drawRoundedRect(track, 1.5, 1.5);

        if (frac > 0.001) {
            QRectF filled = track;
            filled.setWidth(track.width() * frac);
            painter.setBrush(theme::kAccent);
            painter.drawRoundedRect(filled, 1.5, 1.5);

            if (active) {
                // A travelling highlight on the filled portion.
                const qreal hx = filled.left() + (filled.width()) * phase_;
                QColor spark = theme::kAccentSoft;
                spark.setAlphaF(0.9);
                painter.setBrush(spark);
                painter.drawEllipse(QPointF(std::min(hx, filled.right()), track.center().y()),
                                    2.4, 2.4);
            }
        }
    }
}

}  // namespace mimi::ui
