#pragma once

#include "audio/capture.hpp"
#include "brain/ollama.hpp"
#include "brain/router.hpp"
#include "ui/chat_view.hpp"
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

protected:
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void onState(int state);
    void onHeard(const QString& text, bool followUp);
    void onSubmit();
    void onMicClicked();
    void onListenToggled(bool listening);

private:
    QWidget* buildSidebar();
    QWidget* buildChatPanel();
    // Runs the router on a worker thread and posts the reply back. Routing can
    // block for seconds on an Ollama call, so it must never touch the GUI thread.
    void respond(const QString& prompt);
    void deliver(const QString& reply, const QString& action, bool acted);
    void say(const QString& text);

    ChatView* chat_ = nullptr;
    VoiceOrb* orb_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* mic_badge_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QLineEdit* input_ = nullptr;
    QPushButton* mic_ = nullptr;
    QPushButton* power_ = nullptr;

    VoiceBridge* bridge_ = nullptr;
    std::unique_ptr<audio::Capture> capture_;
    std::unique_ptr<voice::Listener> listener_;
    std::unique_ptr<voice::Speaker> speaker_;
    std::unique_ptr<brain::Ollama> ollama_;
    std::unique_ptr<brain::Router> router_;

    int pending_bubble_ = 0;
};

}  // namespace mimi::ui
