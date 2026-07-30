#pragma once

#include "ui/presence.hpp"

#include <QPixmap>
#include <QVector>
#include <QWidget>

class QTimer;

namespace mimi::ui {

// The bottom layer of the interface, and the app's ambient nervous system.
//
// A graphite surface lit by two very slow pools of blue light -- glacial on
// purpose, so at a glance it looks still but is never *frozen*, which is most
// of what separates a live product from a screenshot of one. Painted with
// alpha so the native vibrancy underneath still contributes real depth.
//
// On top of that resting drift it carries one motion signature per presence,
// so the room itself reflects what Mimi is doing without a single word:
//
//   Observing    the resting drift alone, near-black and calm
//   Listening    a slow blue wave breathing out from her, riding the mic level
//   Thinking      a field of drifting particles -- thought, made weather
//   Responding   soft flowing lines, the current moving through the room
//   Remembering  the particles drawn inward, an idea being filed away
//
// Signatures cross-fade rather than switch: every transition is weighed in and
// out over ~kFadeMs, so nothing about the background ever snaps. It is meant to
// be felt, not seen.
class AmbientCanvas : public QWidget {
    Q_OBJECT

public:
    explicit AmbientCanvas(QWidget* parent = nullptr);

public Q_SLOTS:
    void setPresence(Presence presence);
    void setLevel(float rms);  // 0..1 from the audio thread

protected:
    void paintEvent(QPaintEvent*) override;
    // The render loop is a running cost; when the window is tucked away to the
    // puck there is nothing to draw, so stop the clock entirely.
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    // One drifting mote of the particle field, reused across Thinking (loose
    // brownian drift) and Remembering (pulled toward her centre).
    struct Mote {
        qreal x, y;    // normalised 0..1 across the surface
        qreal vx, vy;  // velocity per tick, in the same units
        qreal seed;    // per-mote phase, so they twinkle out of step
    };

    // The motion signatures, each faded independently so two can overlap
    // mid-transition. Index into weight_.
    enum Signature { SigWave, SigParticle, SigLine, SigConverge, SigCount };

    void step();
    void paintWave(QPainter&, const QPointF& heart, qreal w, qreal h, qreal weight);
    void paintParticles(QPainter&, qreal w, qreal h, qreal drift, qreal pull);
    void paintLines(QPainter&, qreal w, qreal h, qreal weight);

    QTimer* tick_ = nullptr;
    qreal phase_ = 0.0;      // one slow cycle a minute, drives the resting drift
    qreal flow_ = 0.0;       // faster clock for waves and lines
    qreal level_ = 0.0;      // smoothed mic level
    qreal mute_ = 0.0;       // 0 live .. 1 fully muted, eased

    Presence presence_ = Presence::Observing;
    qreal weight_[SigCount] = {0, 0, 0, 0};  // current, eased toward the target

    QVector<Mote> motes_;
    // A soft round dot, rendered once. Blitting it per mote is far cheaper than
    // building a radial gradient for every particle on every frame.
    QPixmap moteSprite_;
};

}  // namespace mimi::ui
