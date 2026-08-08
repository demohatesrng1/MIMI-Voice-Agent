#pragma once

#include "audio/capture.hpp"
#include "brain/ollama.hpp"
#include "brain/router.hpp"
#include "ui/ambient_audio.hpp"
#include "ui/floating_orb.hpp"
#include "ui/home_view.hpp"
#include "ui/presence.hpp"
#include "ui/voice_bridge.hpp"
#include "voice/listener.hpp"
#include "voice/tts.hpp"

#include <QMainWindow>
#include <memory>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace mimi::ui {

class AmbientCanvas;
class CommandBar;
class CommandPalette;
class ContextRibbon;
class GhostButton;
class NeuralSearch;
class SettingsView;
class NotesView;
class TimelineView;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void startVoice();
    // The window has exactly two sizes: this one, and full screen.
    void applyFixedSize();
    void toggleFullScreen();
    // Native title-bar treatment. Must run after show(), which is when a real
    // NSWindow exists to configure.
    void applyNativeChrome();

protected:
    void closeEvent(QCloseEvent* event) override;
    // Re-applies the native chrome, since Qt can rebuild or restyle the
    // NSWindow across hide/show and full-screen transitions.
    void showEvent(QShowEvent* event) override;
    // Keeps the floating overlays (AI dock, command palette) placed as the
    // window resizes; they are not in the layout.
    void resizeEvent(QResizeEvent* event) override;

private Q_SLOTS:
    void onState(int state);
    void onHeard(const QString& text, bool followUp);
    void onMicClicked();
    void toggleWindow();

private:
    QWidget* buildTitleBar();
    void navigate(int page);
    void refreshContext();
    void setMuted(bool muted);
    void ask(const QString& utterance);
    void deliver(const QString& reply, const QString& action, bool acted);
    void say(const QString& text);
    void note(const QString& message);
    // Drive every surface -- orb, hero, status capsule, living background --
    // from one presence, so nothing on screen ever disagrees about her state.
    void applyPresence(Presence presence);
    // The beat after an answer where she files it away. Held briefly, then the
    // real voice state takes over again.
    void flashRemembering();
    // Position the floating overlays against the current window size.
    void layoutOverlays();
    // Route a tap on the AI dock to the faculty it stands for.
    // Adaptive UI: show the power surfaces in Expert, hide them in Simple.

    QStackedWidget* pages_ = nullptr;
    AmbientCanvas* ambient_ = nullptr;
    ContextRibbon* ribbon_ = nullptr;
    HomeView* home_ = nullptr;
    TimelineView* timeline_ = nullptr;
    NotesView* notes_ = nullptr;
    SettingsView* settings_ = nullptr;
    AmbientAudio audio_;

    QLabel* statusDot_ = nullptr;
    QWidget* presenceDot_ = nullptr;
    QLabel* statusText_ = nullptr;
    GhostButton* navHome_ = nullptr;
    GhostButton* navTimeline_ = nullptr;
    GhostButton* navNotes_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QPushButton* voicePill_ = nullptr;
    bool speakReplies_ = true;
    QPushButton* mutePill_ = nullptr;
    GhostButton* settingsBtn_ = nullptr;
    CommandBar* composer_ = nullptr;

    CommandPalette* palette_ = nullptr;
    NeuralSearch* search_ = nullptr;

    FloatingOrb* puck_ = nullptr;
    VoiceBridge* bridge_ = nullptr;

    // The last state the voice engine reported, so presence can be restored
    // after a transient overlay like Remembering.
    voice::State voiceState_ = voice::State::Idle;
    QTimer* rememberTimer_ = nullptr;
    QTimer* contextTimer_ = nullptr;
    bool remembering_ = false;
    // Whether any non-zero audio has ever arrived. A denied microphone yields
    // exact zeros for ever, so this is the backstop for the case where the
    // permission reads as granted but the input device is silent anyway.
    bool heardAnything_ = false;
    std::unique_ptr<audio::Capture> capture_;
    std::unique_ptr<voice::Listener> listener_;
    std::unique_ptr<voice::Speaker> speaker_;
    std::unique_ptr<brain::Ollama> ollama_;
    std::unique_ptr<brain::Router> router_;

    // The utterance currently being answered, so the reply can be shown
    // against what was asked.
    QString pending_;
};

}  // namespace mimi::ui
