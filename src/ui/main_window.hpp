#pragma once

#include "audio/capture.hpp"
#include "brain/ollama.hpp"
#include "brain/router.hpp"
#include "ui/chat_view.hpp"
#include "ui/floating_orb.hpp"
#include "ui/voice_bridge.hpp"
#include "ui/voice_orb.hpp"
#include "voice/listener.hpp"
#include "voice/tts.hpp"

#include <QMainWindow>
#include <memory>

class QLabel;
class QLineEdit;
class QPushButton;

namespace mimi::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Brings up the mic, models and listening loop. Reports failures into the
    // transcript rather than throwing, so a missing model shows up as something
    // the user can read instead of a window that never appears.
    void startVoice();

    // Applies the native title-bar treatment. Must run after show(), because it
    // needs a real NSWindow to configure.
    void applyNativeChrome();

protected:
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void onState(int state);
    void onHeard(const QString& text, bool followUp);
    void onSubmit();
    void onMicClicked();
    void onListenToggled(bool listening);
    void toggleWindow();

private:
    QWidget* buildTitleBar();
    QWidget* buildRail();
    QWidget* buildChatPanel();
    QWidget* buildComposer();
    // Runs the router on a worker thread and posts the reply back. Routing can
    // block for seconds on an Ollama call, so it must never touch the GUI thread.
    void respond(const QString& prompt);
    void deliver(const QString& reply, const QString& action, bool acted);
    void say(const QString& text);
    void clearSuggestions();

    ChatView* chat_ = nullptr;
    VoiceOrb* orb_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* statusDot_ = nullptr;
    QLabel* micBadge_ = nullptr;
    QLineEdit* input_ = nullptr;
    QPushButton* mic_ = nullptr;
    QPushButton* power_ = nullptr;
    QWidget* suggestions_ = nullptr;

    FloatingOrb* puck_ = nullptr;
    VoiceBridge* bridge_ = nullptr;
    std::unique_ptr<audio::Capture> capture_;
    std::unique_ptr<voice::Listener> listener_;
    std::unique_ptr<voice::Speaker> speaker_;
    std::unique_ptr<brain::Ollama> ollama_;
    std::unique_ptr<brain::Router> router_;
};

}  // namespace mimi::ui
