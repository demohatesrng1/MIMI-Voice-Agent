#pragma once

#include "voice/listener.hpp"

#include <QColor>
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
    void setState(int state);  // mimi::voice::State
    void setLevel(float rms);  // 0..1 from the audio thread

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor accent() const;

    voice::State state_ = voice::State::Idle;
    qreal phase_ = 0.0;
    qreal angle_ = 0.0;  // degrees, driven by spin_
    // Smoothed, because raw 80 ms RMS jitters hard enough to look like a fault.
    qreal level_ = 0.0;

    QVector<qreal> levels_;  // ring buffer of recent levels, for the spokes
    int cursor_ = 0;

    QPropertyAnimation* pulse_ = nullptr;
    QPropertyAnimation* spin_ = nullptr;
};

}  // namespace mimi::ui
