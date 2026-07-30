#pragma once

#include "ui/presence.hpp"
#include "ui/workspace_dock.hpp"

#include <QString>
#include <QWidget>

class QLabel;

namespace mimi::ui {

class VoiceOrb;
class ConfidenceMeter;
class LiveThinking;
class PredictiveActions;

// The main surface.
//
// Deliberately not a chat transcript. You do not scroll back through a
// conversation with an assistant -- you glance at what she is doing now and
// reach for what she can do next. So: her, large and alive at the centre;
// the single exchange in progress, in type big enough to read across a desk;
// a confidence read-out under the answer; and the workspace dock, which
// rearranges itself around what you are doing.
class HomeView : public QWidget {
    Q_OBJECT

public:
    explicit HomeView(QWidget* parent = nullptr);

    void setPresence(Presence presence);
    void setLevel(float rms);
    // The current exchange. Either may be empty.
    void setExchange(const QString& said, const QString& replied);
    void setThinking();
    // 0..1 to show under the answer; a negative value clears it.
    void setConfidence(qreal value);

    WorkspaceDock* workspace() const { return dock_; }

Q_SIGNALS:
    // A suggestion or tool was pressed; the text is fed through the router
    // exactly as if it had been spoken.
    void commandRequested(QString utterance);

private:
    VoiceOrb* orb_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* saidLabel_ = nullptr;
    QLabel* replyLabel_ = nullptr;
    LiveThinking* live_ = nullptr;
    ConfidenceMeter* confidence_ = nullptr;
    PredictiveActions* predictive_ = nullptr;
    WorkspaceDock* dock_ = nullptr;
};

}  // namespace mimi::ui
