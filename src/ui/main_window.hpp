#pragma once

#include "audio/capture.hpp"
#include "brain/ollama.hpp"
#include "brain/router.hpp"
#include "ui/activity_view.hpp"
#include "ui/floating_orb.hpp"
#include "ui/home_view.hpp"
#include "ui/nav_rail.hpp"
#include "ui/skills_view.hpp"
#include "ui/voice_bridge.hpp"
#include "voice/listener.hpp"
#include "voice/tts.hpp"

#include <QMainWindow>
#include <memory>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace mimi::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void startVoice();
    // Native title-bar treatment. Must run after show(), which is when a real
    // NSWindow exists to configure.
    void applyNativeChrome();

protected:
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void onState(int state);
    void onHeard(const QString& text, bool followUp);
    void onSubmit();
    void onMicClicked();
    void toggleWindow();

private:
    QWidget* buildTitleBar();
    void ask(const QString& utterance);
    void deliver(const QString& reply, const QString& action, bool acted);
    void say(const QString& text);
    void note(const QString& message);

    NavRail* rail_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    HomeView* home_ = nullptr;
    ActivityView* activity_ = nullptr;
    SkillsView* skills_ = nullptr;
    QWidget* settings_ = nullptr;

    QLabel* statusDot_ = nullptr;
    QLabel* statusText_ = nullptr;
    QLabel* micBadge_ = nullptr;
    QLineEdit* input_ = nullptr;
    QPushButton* mic_ = nullptr;

    FloatingOrb* puck_ = nullptr;
    VoiceBridge* bridge_ = nullptr;
    std::unique_ptr<audio::Capture> capture_;
    std::unique_ptr<voice::Listener> listener_;
    std::unique_ptr<voice::Speaker> speaker_;
    std::unique_ptr<brain::Ollama> ollama_;
    std::unique_ptr<brain::Router> router_;

    // The utterance currently being answered, so the reply can be paired with
    // it in the activity log.
    QString pending_;
};

}  // namespace mimi::ui
