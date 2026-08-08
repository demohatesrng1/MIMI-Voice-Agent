#pragma once

#include "ui/presence.hpp"
#include "ui/workspace_dock.hpp"

#include <QString>
#include <QWidget>

class QLabel;
class QTimer;

namespace mimi::ui {

class VoiceOrb;
class AvatarView;
class LiveThinking;
class LuminOrb;
class StageCard;
class NeuralSidebar;
class CommandBar;

// The main surface.
//
// Deliberately not a chat transcript. You do not scroll back through a
// conversation with an assistant -- you glance at what she is doing now and
// reach for what she can do next. So: her, large and alive at the centre;
// the single exchange in progress, in type big enough to read across a desk;
// and the workspace dock, which
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

protected:
    // The stage is positioned by hand, not laid out -- see layoutStage().
    void resizeEvent(QResizeEvent* event) override;

public:

    WorkspaceDock* workspace() const { return dock_; }
    // Null when this build has no WebEngine or no .vrm was found, in which
    // case the 2D orb is on screen instead.
    AvatarView* avatar() const { return avatar_; }

Q_SIGNALS:
    // A suggestion or tool was pressed; the text is fed through the router
    // exactly as if it had been spoken.
    void commandRequested(QString utterance);
    // The orb was held, or Voice Mode was pressed: start listening now.
    void voiceRequested();

private:
    // Places her column and the caption column against the current size.
    void layoutStage();
    // Restyles everything that borrows the orb's light: the greeting, the
    // primary card, the wordmark.
    void applyLight();

public:
    // Re-reads the machine state on the right-hand panel.
    void refreshSidebar();

private:

    // The stage.
    LuminOrb* lumin_ = nullptr;
    QLabel* greetingLabel_ = nullptr;
    QLabel* marqueeLabel_ = nullptr;
    QTimer* marqueeTimer_ = nullptr;
    int marqueeIndex_ = 0;
    StageCard* cardVoice_ = nullptr;
    StageCard* cardReflect_ = nullptr;
    StageCard* cardBrainstorm_ = nullptr;
    NeuralSidebar* sidebar_ = nullptr;
    CommandBar* composer_ = nullptr;

    VoiceOrb* orb_ = nullptr;
    // Her body. Null unless the build has WebEngine and a model was found;
    // the orb stands in for her when it is.
    AvatarView* avatar_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* saidLabel_ = nullptr;
    QLabel* replyLabel_ = nullptr;
    LiveThinking* live_ = nullptr;
    WorkspaceDock* dock_ = nullptr;
};

}  // namespace mimi::ui
