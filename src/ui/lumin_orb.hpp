#pragma once

#include "ui/presence.hpp"

#include <QColor>
#include <QElapsedTimer>
#include <QVector>
#include <QWidget>

class QTimer;

namespace mimi::ui {

// The orb at the centre of the home screen.
//
// Not a status light -- a presence. Three things happen at once and they are
// deliberately on one clock: an aura that expands and fades every 3 seconds, a
// ring of motes orbiting the core, and a colour that follows the hour rather
// than the state, so the room she sits in changes through the day while the
// background never does.
//
// Attention is the interaction. Hovering pulls the motes inward and speeds them
// up -- she inhales what you are giving her -- and holding for 1.5 seconds
// completes the gesture and opens voice input, bypassing the keyboard entirely.
// The progress ring only appears once you press, so the gesture stays out of
// the way until a finger rests there and then teaches itself.
class LuminOrb : public QWidget {
    Q_OBJECT

public:
    // Which light she is in. Follows the clock unless pinned in Settings.
    enum class Light { Auto, Morning, Afternoon, Night };

    explicit LuminOrb(QWidget* parent = nullptr);

    QSize sizeHint() const override { return {260, 260}; }
    QSize minimumSizeHint() const override { return {170, 170}; }

    // The accent for the hour, so the rest of the page can borrow it: the
    // primary card, the wordmark and the greeting all sit in the same light.
    QColor accent() const;
    // Cyan when she is with you, violet while she thinks, eased between.
    QColor stateAccent() const;
    QColor accentCore() const;
    // "Good morning" / "Good afternoon" / "Good evening".
    QString greetingWord() const;

    void setLight(Light light);
    Light light() const noexcept { return light_; }

public Q_SLOTS:
    void setPresence(Presence presence);
    void setLevel(float rms);

Q_SIGNALS:
    // The hold completed: open voice input.
    void held();
    // The hour rolled over, or the pinned light changed. The page restyles.
    void lightChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void tick();
    // Which of the three palettes applies right now.
    Light resolved() const;

    struct Mote {
        double angle = 0;
        double base = 0;    // resting orbit radius, as a fraction of the core
        double radius = 0;
        double speed = 0;
        double tilt = 0;    // flattens the orbit into an ellipse
        double phase = 0;
    };

    QVector<Mote> motes_;
    QTimer* clock_ = nullptr;
    QElapsedTimer elapsed_;
    qint64 lastMs_ = 0;

    Light light_ = Light::Auto;
    int lastHour_ = -1;

    Presence presence_ = Presence::Observing;
    double level_ = 0.0;
    double phase_ = 0.0;      // the 3s breath, 0..1
    bool inhaling_ = false;
    double inhale_ = 0.0;     // eased 0..1

    bool holding_ = false;
    double hold_ = 0.0;       // 0..1 over 1.5s
    double shake_ = 0.0;      // decays after a completed hold
    double think_ = 0.0;      // 0 cyan, 1 violet
    double bob_ = 0.0;        // the float, 0..1
};

}  // namespace mimi::ui
