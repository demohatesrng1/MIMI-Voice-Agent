#pragma once

#include "ui/presence.hpp"

#include <QColor>
#include <QPixmap>
#include <QPointF>
#include <QVector>
#include <QWidget>

class QPropertyAnimation;

namespace mimi::ui {

// The status light of the whole app.
//
// A text label saying "listening" is not enough feedback for a voice interface:
// the user needs to know Mimi heard them before they finish the sentence. The
// orb answers three questions at a glance -- is she awake, is she hearing me
// right now, and is she busy -- through colour, pulse rate, and a ring of
// spokes carrying the last few seconds of microphone level.
//
// She also holds your gaze: the face drifts a couple of pixels toward the
// cursor, so she reads as looking *at* what you are doing rather than staring
// past you. Small on purpose -- eye contact you feel, not a mascot that
// tracks the mouse.
class VoiceOrb : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal phase READ phase WRITE setPhase)
    Q_PROPERTY(qreal spin READ spin WRITE setSpin)

public:
    explicit VoiceOrb(QWidget* parent = nullptr);

    QSize sizeHint() const override { return {210, 210}; }
    QSize minimumSizeHint() const override { return {150, 150}; }

    qreal phase() const noexcept { return phase_; }
    void setPhase(qreal phase);
    qreal spin() const noexcept { return angle_; }
    void setSpin(qreal spin);

public slots:
    void setPresence(Presence presence);
    void setLevel(float rms);  // 0..1 from the audio thread

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor accent() const;
    // Ease the gaze offset toward wherever the cursor is. Called off the pulse
    // animation, which already ticks at ~60 fps, so it needs no timer of its own.
    void trackGaze();
    // The app art, masked to a circle at the size we need. Rebuilt only when
    // the diameter changes; masking every frame would be wasteful at 12 fps.
    const QPixmap& portrait(int diameter) const;

    mutable QPixmap portrait_cache_;
    mutable int portrait_size_ = 0;

    Presence presence_ = Presence::Observing;
    qreal phase_ = 0.0;
    qreal angle_ = 0.0;  // degrees, driven by spin_
    // Smoothed, because raw 80 ms RMS jitters hard enough to look like a fault.
    qreal level_ = 0.0;
    // Where she is looking: -1..1 in each axis, eased toward the cursor.
    QPointF gaze_{0.0, 0.0};

    QVector<qreal> levels_;  // ring buffer of recent levels, for the spokes
    int cursor_ = 0;

    QPropertyAnimation* pulse_ = nullptr;
    QPropertyAnimation* spin_ = nullptr;
};

}  // namespace mimi::ui
