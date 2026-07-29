#pragma once

#include "ui/voice_orb.hpp"

#include <QPoint>
#include <QWidget>

class QMenu;

namespace mimi::ui {

// A small always-on-top puck that lives above every other window.
//
// The point of an always-listening assistant is that you do not go to her, so
// having her only exist inside a window you have to find defeats it. This is a
// circular, frameless, click-through-free window that shows the same state the
// main orb does: colour for what she is doing, ring for what she is hearing.
//
// Behaviour:
//   drag         move it anywhere, it snaps to the nearest screen edge
//   click        show / hide the main window
//   double click start listening straight away, no wake word
//   right click  mute, open, quit
//
// Qt::Tool keeps it out of the Dock and the app switcher, which is what makes
// it read as an overlay rather than a second application.
class FloatingOrb : public QWidget {
    Q_OBJECT

public:
    explicit FloatingOrb(QWidget* parent = nullptr);

    void setState(int state);
    void setLevel(float rms);

    // Puts it near a screen corner on first run.
    void moveToDefaultCorner();

Q_SIGNALS:
    void clicked();        // toggle the main window
    void doubleClicked();  // push-to-talk
    void muteRequested(bool muted);
    void quitRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void snapToEdge();

    VoiceOrb* orb_ = nullptr;
    QMenu* menu_ = nullptr;

    QPoint press_offset_;
    bool dragging_ = false;
    // A press that never moved is a click; anything past this is a drag, so a
    // slightly shaky click still opens the window.
    bool moved_beyond_threshold_ = false;
    bool hovered_ = false;
    bool muted_ = false;
};

}  // namespace mimi::ui
