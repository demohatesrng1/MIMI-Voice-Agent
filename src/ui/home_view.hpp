#pragma once

#include <QString>
#include <QWidget>

class QLabel;

namespace mimi::ui {

class VoiceOrb;

// The main surface.
//
// Deliberately not a chat transcript. You do not scroll back through a
// conversation with an assistant -- you glance at what she is doing now and
// reach for what she can do next. So: her, large and reactive; the single
// exchange in progress, in type big enough to read across a desk; and the
// common commands as tiles, because a voice product that does not tell you
// what to say is unusable on the first day.
//
// History still exists, in Activity, where a log belongs.
class HomeView : public QWidget {
    Q_OBJECT

public:
    explicit HomeView(QWidget* parent = nullptr);

    void setState(int state);
    void setLevel(float rms);
    // The current exchange. Either may be empty.
    void setExchange(const QString& said, const QString& replied);
    void setThinking();

Q_SIGNALS:
    // A quick-action tile was pressed; the text is fed through the router
    // exactly as if it had been spoken.
    void commandRequested(QString utterance);

private:
    QWidget* buildTiles();

    VoiceOrb* orb_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* saidLabel_ = nullptr;
    QLabel* replyLabel_ = nullptr;
};

}  // namespace mimi::ui
