#pragma once

#include "voice/listener.hpp"

#include <QPixmap>
#include <QPoint>
#include <QWidget>

class QMenu;
class QPropertyAnimation;

namespace mimi::ui {

// A small always-on-top puck that lives above every other window.
//
// The point of an always-listening assistant is that you do not go to her, so
// having her only exist inside a window you have to find defeats it.
//
// The artwork fills the whole disc rather than sitting in the middle of one:
// at this size a portrait inset inside a frame reads as a generic widget, while
// a full-bleed circle reads as *her*. Everything the orb needs to say -- what
// she is doing, what she is hearing, whether she is muted -- is carried by the
// ring around the edge instead of by anything drawn on top of her.
//
//   drag         move it, snaps to the nearest screen edge
//   click        show / hide the main window
//   double click start listening straight away, no wake word
//   right click  mute, open, quit
class FloatingOrb : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal phase READ phase WRITE setPhase)
    Q_PROPERTY(qreal spin READ spin WRITE setSpin)

public:
    explicit FloatingOrb(QWidget* parent = nullptr);

    void setState(int state);
    void setLevel(float rms);
    void moveToDefaultCorner();
    // Keeps the orb on screen across app switches and Spaces. Safe to call
    // repeatedly, and a no-op off macOS.
    void pinToAllSpaces();

    qreal phase() const noexcept { return phase_; }
    void setPhase(qreal phase);
    qreal spin() const noexcept { return angle_; }
    void setSpin(qreal spin);

Q_SIGNALS:
    void clicked();
    void doubleClicked();
    void muteRequested(bool muted);
    void quitRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QColor accent() const;
    // Circle-masked artwork at the current size. Cached: masking on every frame
    // would be pointless work twelve times a second.
    const QPixmap& portrait() const;
    void snapToEdge();

    voice::State state_ = voice::State::Idle;
    qreal phase_ = 0.0;
    qreal angle_ = 0.0;
    qreal level_ = 0.0;

    mutable QPixmap portrait_cache_;
    mutable QPixmap muted_cache_;

    QMenu* menu_ = nullptr;
    QPropertyAnimation* pulse_ = nullptr;
    QPropertyAnimation* spin_ = nullptr;

    QPoint press_offset_;
    bool dragging_ = false;
    bool moved_beyond_threshold_ = false;
    bool hovered_ = false;
    bool muted_ = false;
};

}  // namespace mimi::ui
