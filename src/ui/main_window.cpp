#include "ui/main_window.hpp"

#include "core/log.hpp"
#include "core/paths.hpp"
#include "ui/theme.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <thread>

namespace mimi::ui {
namespace {
constexpr std::string_view kTag = "ui";
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Mimi ミミ"));
    resize(1040, 700);
    setMinimumSize(820, 560);

    auto* central = new QWidget;
    central->setObjectName(QStringLiteral("root"));
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildSidebar());
    layout->addWidget(buildChatPanel(), 1);
    setCentralWidget(central);

    // The always-on-top puck. Deliberately parentless as a widget so it is its
    // own top-level window, but owned by this object's lifetime.
    puck_ = new FloatingOrb;
    puck_->moveToDefaultCorner();
    puck_->show();
    connect(puck_, &FloatingOrb::clicked, this, &MainWindow::toggleWindow);
    connect(puck_, &FloatingOrb::doubleClicked, this, &MainWindow::onMicClicked);
    connect(puck_, &FloatingOrb::muteRequested, this,
            [this](bool muted) { power_->setChecked(!muted); });
    connect(puck_, &FloatingOrb::quitRequested, qApp, &QApplication::quit);

    bridge_ = new VoiceBridge(this);
    connect(bridge_, &VoiceBridge::stateChanged, this, &MainWindow::onState);
    connect(bridge_, &VoiceBridge::levelChanged, this, [this](float rms, float) {
        orb_->setLevel(rms);
        puck_->setLevel(rms);
    });
    connect(bridge_, &VoiceBridge::heard, this, &MainWindow::onHeard);
    connect(bridge_, &VoiceBridge::bargedIn, this, [this] {
        if (speaker_) speaker_->stop();
    });
}

MainWindow::~MainWindow() {
    if (listener_) listener_->stop();
    if (capture_) capture_->stop();
    delete puck_;  // top-level, so it is not destroyed with the window
}

void MainWindow::toggleWindow() {
    if (isVisible() && !isMinimized()) {
        hide();
        return;
    }
    showNormal();
    raise();
    activateWindow();
}

QWidget* MainWindow::buildSidebar() {
    auto* side = new QWidget;
    side->setObjectName(QStringLiteral("sidebar"));
    side->setFixedWidth(248);

    auto* layout = new QVBoxLayout(side);
    layout->setContentsMargins(20, 28, 20, 20);
    layout->setSpacing(0);

    orb_ = new VoiceOrb;
    layout->addWidget(orb_, 0, Qt::AlignHCenter);
    layout->addSpacing(16);

    auto* title = new QLabel(QStringLiteral("Mimi"));
    title->setObjectName(QStringLiteral("brand"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    subtitle_ = new QLabel(QStringLiteral("ミミ"));
    subtitle_->setObjectName(QStringLiteral("brandJp"));
    subtitle_->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle_);
    layout->addSpacing(10);

    status_ = new QLabel(QStringLiteral("STARTING"));
    status_->setObjectName(QStringLiteral("status"));
    status_->setAlignment(Qt::AlignCenter);
    status_->setFixedWidth(132);
    layout->addWidget(status_, 0, Qt::AlignHCenter);

    layout->addSpacing(22);
    auto* rule = new QFrame;
    rule->setFrameShape(QFrame::HLine);
    rule->setObjectName(QStringLiteral("rule"));
    layout->addWidget(rule);
    layout->addSpacing(16);

    auto* hint = new QLabel(QStringLiteral(
        "「ねえミミ」と話しかけてください。<br><br>"
        "Say <b>ねえミミ</b> (or &ldquo;hey mimi&rdquo;)<br>followed by a command."));
    hint->setObjectName(QStringLiteral("hint"));
    hint->setTextFormat(Qt::RichText);  // otherwise the tags render literally
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignLeft);
    layout->addWidget(hint);

    layout->addStretch(1);

    power_ = new QPushButton(QStringLiteral("Listening"));
    power_->setObjectName(QStringLiteral("power"));
    power_->setCheckable(true);
    power_->setChecked(true);
    power_->setCursor(Qt::PointingHandCursor);
    connect(power_, &QPushButton::toggled, this, &MainWindow::onListenToggled);
    layout->addWidget(power_);

    return side;
}

QWidget* MainWindow::buildChatPanel() {
    auto* panel = new QWidget;
    panel->setObjectName(QStringLiteral("chatPanel"));

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* top = new QWidget;
    top->setObjectName(QStringLiteral("topBar"));
    top->setFixedHeight(46);
    auto* top_layout = new QHBoxLayout(top);
    top_layout->setContentsMargins(20, 0, 16, 0);

    auto* top_title = new QLabel(QStringLiteral("CONVERSATION"));
    top_title->setObjectName(QStringLiteral("topTitle"));
    top_layout->addWidget(top_title);
    top_layout->addStretch(1);

    // The microphone is open whenever Mimi is listening. That has to be
    // visible at all times, not buried in a settings pane.
    mic_badge_ = new QLabel(QStringLiteral("● MIC LIVE"));
    mic_badge_->setObjectName(QStringLiteral("micBadge"));
    top_layout->addWidget(mic_badge_);

    layout->addWidget(top);

    chat_ = new ChatView;
    layout->addWidget(chat_, 1);

    auto* bar = new QWidget;
    bar->setObjectName(QStringLiteral("inputBar"));
    auto* bar_layout = new QHBoxLayout(bar);
    bar_layout->setContentsMargins(18, 14, 18, 16);
    bar_layout->setSpacing(10);

    mic_ = new QPushButton(QStringLiteral("🎙"));
    mic_->setObjectName(QStringLiteral("mic"));
    mic_->setFixedSize(42, 42);
    mic_->setCursor(Qt::PointingHandCursor);
    mic_->setToolTip(QStringLiteral("Speak now, no wake word needed"));
    connect(mic_, &QPushButton::clicked, this, &MainWindow::onMicClicked);
    bar_layout->addWidget(mic_);

    input_ = new QLineEdit;
    input_->setObjectName(QStringLiteral("input"));
    input_->setPlaceholderText(QStringLiteral("何かきいてください…  (or type a command)"));
    connect(input_, &QLineEdit::returnPressed, this, &MainWindow::onSubmit);
    bar_layout->addWidget(input_, 1);

    auto* send = new QPushButton(QStringLiteral("Send"));
    send->setObjectName(QStringLiteral("send"));
    send->setCursor(Qt::PointingHandCursor);
    connect(send, &QPushButton::clicked, this, &MainWindow::onSubmit);
    bar_layout->addWidget(send);

    layout->addWidget(bar);
    return panel;
}

void MainWindow::startVoice() {
    const auto& models = paths::models_dir();

    voice::Listener::Config config;
    config.wake_backend = voice::Listener::WakeBackend::PhraseSpotter;
    config.vad_model = models / "silero_vad.onnx";
    config.whisper_model = models / "ggml-small.bin";
    config.language = "ja";
    // Left empty on purpose: ggml-tiny cannot spot the wake word in Japanese,
    // so the accurate model does the gating and the command in one decode.
    config.gate_model.clear();

    try {
        ollama_ = std::make_unique<brain::Ollama>(brain::Ollama::Config{});
        router_ = std::make_unique<brain::Router>(*ollama_);
        router_->on_reminder([this](const std::string& text) {
            const QString message = QString::fromStdString(text);
            QMetaObject::invokeMethod(this, [this, message] {
                chat_->append(Speaker::Mimi, QStringLiteral("⏰ %1").arg(message));
                say(message);
            }, Qt::QueuedConnection);
        });

        capture_ = std::make_unique<audio::Capture>();
        capture_->start();

        speaker_ = std::make_unique<voice::Speaker>(voice::Speaker::Config{});
        listener_ = std::make_unique<voice::Listener>(*capture_, std::move(config));
        bridge_->attach(*listener_);

        const int loading = chat_->append(Speaker::System, QStringLiteral("Loading models…"));

        // Warm up off the GUI thread so the window paints immediately instead
        // of appearing frozen for the second whisper takes to load.
        QTimer::singleShot(0, this, [this, loading] {
            // Start Ollama if it is not already up, rather than telling the
            // user to do it. Nothing on a stock machine launches it.
            const bool brain_up = ollama_->ensure_running();
            const bool model_ok = brain_up && ollama_->model_available();
            if (model_ok) ollama_->warmup();

            // VOICEVOX if it is installed; otherwise the system voice, which
            // on this machine is compact quality and sounds it.
            if (speaker_ && !speaker_->using_voicevox()) speaker_->start_voicevox();

            listener_->warmup();
            listener_->start();
            chat_->remove(loading);

            if (!brain_up) {
                chat_->append(Speaker::System,
                              QStringLiteral("Ollama を起動できませんでした。"
                                             "インストールされているか確認してください。"));
            } else if (!model_ok) {
                chat_->append(
                    Speaker::System,
                    QStringLiteral("モデル %1 がありません。`ollama pull %1` を実行して"
                                   "ください。")
                        .arg(QString::fromStdString(ollama_->config().model)));
            }
            status_->setText(QStringLiteral("listening"));
            chat_->append(Speaker::Mimi,
                          QStringLiteral("こんにちは。ミミです。\n"
                                         "「ねえミミ」と呼んでください。"));
            say(QStringLiteral("こんにちは。ミミです。"));
        });
    } catch (const std::exception& e) {
        log::error(kTag, "voice startup failed: {}", e.what());
        status_->setText(QStringLiteral("microphone unavailable"));
        chat_->append(Speaker::System,
                      QStringLiteral("Voice is offline: %1").arg(QString::fromUtf8(e.what())));
    }
}

void MainWindow::onState(int state) {
    orb_->setState(state);
    if (puck_ != nullptr) puck_->setState(state);
    const auto value = static_cast<voice::State>(state);
    // Each state gets its own colour, matching the orb, so the pill and the orb
    // always agree without the user having to read either.
    const char* label = "LISTENING";
    const char* colour = "#4dd8e6";
    switch (value) {
        case voice::State::Idle:      label = "LISTENING";  colour = "#4dd8e6"; break;
        case voice::State::Listening: label = "HEARING YOU"; colour = "#4dd8e6"; break;
        case voice::State::Thinking:  label = "THINKING";   colour = "#f5c45e"; break;
        case voice::State::Speaking:  label = "SPEAKING";   colour = "#58e28b"; break;
        case voice::State::Paused:    label = "MUTED";      colour = "#8890b5"; break;
    }
    status_->setText(QString::fromLatin1(label));
    status_->setStyleSheet(
        QStringLiteral("color:%1; border-color:%1; background:rgba(255,255,255,14);")
            .arg(QString::fromLatin1(colour)));

    // Keep the button honest. blockSignals stops this from bouncing back into
    // onListenToggled and pausing the listener we just heard from.
    if (power_ != nullptr) {
        const bool live = value != voice::State::Paused;
        QSignalBlocker blocker(power_);
        power_->setChecked(live);
        power_->setText(live ? QStringLiteral("Listening") : QStringLiteral("Muted"));
    }

    if (mic_badge_ != nullptr) {
        const bool live = value != voice::State::Paused;
        mic_badge_->setText(live ? QStringLiteral("● MIC LIVE") : QStringLiteral("○ MIC OFF"));
        mic_badge_->setStyleSheet(
            live ? QString()
                 : QStringLiteral("color:#5a6291; border-color:rgba(90,98,145,110);"
                                  "background:transparent;"));
    }
}

void MainWindow::onHeard(const QString& text, bool followUp) {
    chat_->append(Speaker::You, followUp ? text + QStringLiteral("  ↩") : text);
    respond(text);
}

void MainWindow::onSubmit() {
    const QString text = input_->text().trimmed();
    if (text.isEmpty()) return;
    input_->clear();
    chat_->append(Speaker::You, text);
    respond(text);
}

void MainWindow::onMicClicked() {
    if (!listener_) return;
    mic_->setEnabled(false);
    status_->setText(QStringLiteral("go ahead…"));

    // capture_once() blocks until the endpointer closes the utterance -- up to
    // ten seconds. Calling it here would freeze the whole interface, so it goes
    // to a worker and the result comes back through the event loop.
    std::thread([this] {
        const auto heard = listener_->capture_once(std::chrono::milliseconds{10000});
        const QString text =
            heard ? QString::fromStdString(*heard) : QString();
        QMetaObject::invokeMethod(this, [this, text] {
            mic_->setEnabled(true);
            if (text.isEmpty()) {
                status_->setText(QStringLiteral("LISTENING"));
                return;
            }
            chat_->append(Speaker::You, text);
            respond(text);
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onListenToggled(bool listening) {
    if (!listener_) {
        // Nothing to drive yet; onState will correct the label once it exists.
        return;
    }
    if (listening) {
        listener_->resume();
    } else {
        listener_->pause();
        if (speaker_) speaker_->stop();
    }
    // The label follows from onState(), so both it and the status pill are
    // written in exactly one place.
}

void MainWindow::respond(const QString& prompt) {
    if (!router_) return;

    status_->setText(QStringLiteral("thinking…"));
    orb_->setState(static_cast<int>(voice::State::Thinking));

    // Detached rather than pooled: at most one utterance is in flight, and the
    // work is a blocking HTTP call, not something worth a thread pool for.
    const std::string utterance = prompt.toStdString();
    std::thread([this, utterance] {
        const auto reply = router_->route(utterance);
        const QString text = QString::fromStdString(reply.text);
        const QString action = QString::fromStdString(reply.action);
        const bool acted = reply.acted;
        QMetaObject::invokeMethod(this, [this, text, action, acted] {
            deliver(text, action, acted);
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::deliver(const QString& reply, const QString& action, bool acted) {
    // A small marker when she actually did something, so "opened it" and
    // "talked about opening it" are not the same bubble.
    chat_->append(Speaker::Mimi, acted ? QStringLiteral("%1  ·  %2").arg(reply, action) : reply);
    say(reply);
}

void MainWindow::say(const QString& text) {
    if (!speaker_ || !listener_) return;
    listener_->set_speaking(true);
    speaker_->speak(text.toStdString(), [this](bool) {
        // Hop back to the GUI thread: AVFoundation calls us from its own.
        QMetaObject::invokeMethod(this, [this] {
            if (listener_) listener_->set_speaking(false);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Closing the window should not stop an assistant that is meant to be
    // always on. Hide instead; the puck stays, and Quit is in its menu.
    if (puck_ != nullptr) {
        hide();
        event->ignore();
        return;
    }
    if (listener_) listener_->stop();
    if (speaker_) speaker_->stop();
    if (capture_) capture_->stop();
    event->accept();
}

}  // namespace mimi::ui
