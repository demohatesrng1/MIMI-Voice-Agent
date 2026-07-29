#include "ui/main_window.hpp"

#include "core/log.hpp"
#include "core/paths.hpp"
#include "ui/mac_window.hpp"
#include "ui/theme.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <array>
#include <thread>

namespace mimi::ui {
namespace {

constexpr std::string_view kTag = "ui";

constexpr int kTitleBarHeight = 46;
constexpr int kRailWidth = 68;

// Shown until the first exchange. Chosen to span the three different kinds of
// thing she does -- read the machine, change it, answer a question -- so the
// range is obvious without a manual.
constexpr std::array<const char*, 4> kSuggestions{
    "今何時ですか",
    "バッテリーは？",
    "ユーチューブを開いて",
    "5分後に休憩と教えて",
};

QFrame* hairline(Qt::Orientation orientation) {
    auto* line = new QFrame;
    line->setObjectName(QStringLiteral("hairline"));
    line->setFrameShape(orientation == Qt::Horizontal ? QFrame::HLine : QFrame::VLine);
    if (orientation == Qt::Horizontal) {
        line->setFixedHeight(1);
    } else {
        line->setFixedWidth(1);
    }
    return line;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Mimi"));
    resize(1080, 720);
    setMinimumSize(880, 600);

    auto* root = new QWidget;
    root->setObjectName(QStringLiteral("root"));

    // The title bar spans the full width above everything, the way a browser's
    // chrome does -- not a strip bolted onto one pane.
    auto* column = new QVBoxLayout(root);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(buildTitleBar());
    column->addWidget(hairline(Qt::Horizontal));

    auto* body = new QWidget;
    auto* row = new QHBoxLayout(body);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(buildRail());
    row->addWidget(hairline(Qt::Vertical));
    row->addWidget(buildChatPanel(), 1);

    column->addWidget(body, 1);
    setCentralWidget(root);

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
    delete puck_;
}

void MainWindow::applyNativeChrome() {
    adopt_native_titlebar(this);
    // Re-inset now that the real button geometry can be measured.
    if (auto* spacer = findChild<QWidget*>(QStringLiteral("trafficLights"))) {
        spacer->setFixedWidth(traffic_light_inset());
    }
}

// ----------------------------------------------------------------- title bar

QWidget* MainWindow::buildTitleBar() {
    auto* bar = new QWidget;
    bar->setObjectName(QStringLiteral("titleBar"));
    bar->setFixedHeight(kTitleBarHeight);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 16, 0);
    layout->setSpacing(0);

    // The traffic lights float over our content with a full-size content view,
    // so reserve their width rather than letting them land on a label.
    auto* lights = new QWidget;
    lights->setObjectName(QStringLiteral("trafficLights"));
    lights->setFixedWidth(78);
    layout->addWidget(lights);

    auto* name = new QLabel(QStringLiteral("Mimi"));
    name->setObjectName(QStringLiteral("titleName"));
    layout->addWidget(name);

    layout->addSpacing(8);
    auto* jp = new QLabel(QStringLiteral("ミミ"));
    jp->setObjectName(QStringLiteral("titleJp"));
    layout->addWidget(jp);

    layout->addStretch(1);

    micBadge_ = new QLabel(QStringLiteral("MIC"));
    micBadge_->setObjectName(QStringLiteral("micBadge"));
    micBadge_->setToolTip(QStringLiteral("The microphone is open"));
    layout->addWidget(micBadge_);
    layout->addSpacing(14);

    // A coloured dot and a lowercase word. Quieter than a filled pill, and the
    // width never changes as the text does, so nothing shifts around it.
    statusDot_ = new QLabel(QStringLiteral("●"));
    statusDot_->setObjectName(QStringLiteral("statusDot"));
    layout->addWidget(statusDot_);
    layout->addSpacing(7);

    status_ = new QLabel(QStringLiteral("starting"));
    status_->setObjectName(QStringLiteral("statusText"));
    layout->addWidget(status_);

    return bar;
}

// ---------------------------------------------------------------------- rail

QWidget* MainWindow::buildRail() {
    auto* rail = new QWidget;
    rail->setObjectName(QStringLiteral("rail"));
    rail->setFixedWidth(kRailWidth);

    auto* layout = new QVBoxLayout(rail);
    layout->setContentsMargins(0, 16, 0, 16);
    layout->setSpacing(0);

    orb_ = new VoiceOrb;
    orb_->setFixedSize(54, 54);
    layout->addWidget(orb_, 0, Qt::AlignHCenter);

    layout->addStretch(1);

    power_ = new QPushButton(QStringLiteral("◉"));
    power_->setObjectName(QStringLiteral("power"));
    power_->setCheckable(true);
    power_->setChecked(true);
    power_->setFixedSize(34, 34);
    power_->setCursor(Qt::PointingHandCursor);
    power_->setToolTip(QStringLiteral("Mute the microphone"));
    connect(power_, &QPushButton::toggled, this, &MainWindow::onListenToggled);
    layout->addWidget(power_, 0, Qt::AlignHCenter);

    return rail;
}

// ----------------------------------------------------------------- chat pane

QWidget* MainWindow::buildChatPanel() {
    auto* panel = new QWidget;
    panel->setObjectName(QStringLiteral("chatPanel"));

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    chat_ = new ChatView;
    layout->addWidget(chat_, 1);

    // An empty pane with a blinking cursor tells a new user nothing about what
    // she can do, so offer a few real commands until the first exchange.
    suggestions_ = new QWidget;
    suggestions_->setObjectName(QStringLiteral("suggestions"));
    auto* chips = new QHBoxLayout(suggestions_);
    chips->setContentsMargins(26, 0, 26, 12);
    chips->setSpacing(8);
    for (const char* text : kSuggestions) {
        auto* chip = new QPushButton(QString::fromUtf8(text));
        chip->setObjectName(QStringLiteral("chip"));
        chip->setCursor(Qt::PointingHandCursor);
        connect(chip, &QPushButton::clicked, this, [this, chip] {
            input_->setText(chip->text());
            onSubmit();
        });
        chips->addWidget(chip);
    }
    chips->addStretch(1);
    layout->addWidget(suggestions_);

    layout->addWidget(hairline(Qt::Horizontal));
    layout->addWidget(buildComposer());
    return panel;
}

QWidget* MainWindow::buildComposer() {
    auto* bar = new QWidget;
    bar->setObjectName(QStringLiteral("composer"));

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(20, 14, 20, 18);
    layout->setSpacing(0);

    // One field with its buttons inside the border, rather than three separate
    // controls in a row. Fewer edges, and it reads as a single place to type.
    auto* field = new QWidget;
    field->setObjectName(QStringLiteral("field"));
    auto* inner = new QHBoxLayout(field);
    inner->setContentsMargins(7, 6, 7, 6);
    inner->setSpacing(8);

    mic_ = new QPushButton(QStringLiteral("🎙"));
    mic_->setObjectName(QStringLiteral("mic"));
    mic_->setFixedSize(30, 30);
    mic_->setCursor(Qt::PointingHandCursor);
    mic_->setToolTip(QStringLiteral("Speak now, no wake word needed"));
    connect(mic_, &QPushButton::clicked, this, &MainWindow::onMicClicked);
    inner->addWidget(mic_);

    input_ = new QLineEdit;
    input_->setObjectName(QStringLiteral("input"));
    input_->setPlaceholderText(QStringLiteral("聞きたいことを入力…"));
    connect(input_, &QLineEdit::returnPressed, this, &MainWindow::onSubmit);
    inner->addWidget(input_, 1);

    auto* send = new QPushButton(QStringLiteral("↵"));
    send->setObjectName(QStringLiteral("send"));
    send->setFixedSize(30, 30);
    send->setCursor(Qt::PointingHandCursor);
    connect(send, &QPushButton::clicked, this, &MainWindow::onSubmit);
    inner->addWidget(send);

    layout->addWidget(field);
    return bar;
}

void MainWindow::clearSuggestions() {
    if (suggestions_ != nullptr && suggestions_->isVisible()) suggestions_->hide();
}

// ------------------------------------------------------------------- startup

void MainWindow::startVoice() {
    const auto& models = paths::models_dir();

    voice::Listener::Config config;
    config.wake_backend = voice::Listener::WakeBackend::PhraseSpotter;
    config.vad_model = models / "silero_vad.onnx";
    config.whisper_model = models / "ggml-small.bin";
    config.language = "ja";
    // Empty on purpose: ggml-tiny cannot spot the wake word in Japanese, so the
    // accurate model does the gating and the command in one decode.
    config.gate_model.clear();

    try {
        ollama_ = std::make_unique<brain::Ollama>(brain::Ollama::Config{});
        router_ = std::make_unique<brain::Router>(*ollama_);
        router_->on_reminder([this](const std::string& text) {
            const QString message = QString::fromStdString(text);
            QMetaObject::invokeMethod(this, [this, message] {
                chat_->append(Speaker::Mimi, QStringLiteral("⏰  %1").arg(message));
                say(message);
            }, Qt::QueuedConnection);
        });

        capture_ = std::make_unique<audio::Capture>();
        capture_->start();

        speaker_ = std::make_unique<voice::Speaker>(voice::Speaker::Config{});
        listener_ = std::make_unique<voice::Listener>(*capture_, std::move(config));
        bridge_->attach(*listener_);

        const int loading = chat_->append(Speaker::System, QStringLiteral("起動中…"));

        QTimer::singleShot(0, this, [this, loading] {
            const bool brain_up = ollama_->ensure_running();
            const bool model_ok = brain_up && ollama_->model_available();
            if (model_ok) ollama_->warmup();
            if (speaker_ && !speaker_->using_voicevox()) speaker_->start_voicevox();

            listener_->warmup();
            listener_->start();
            chat_->remove(loading);

            if (!brain_up) {
                chat_->append(Speaker::System,
                              QStringLiteral("Ollama を起動できませんでした。"));
            } else if (!model_ok) {
                chat_->append(Speaker::System,
                              QStringLiteral("モデル %1 がありません。")
                                  .arg(QString::fromStdString(ollama_->config().model)));
            }
            chat_->append(Speaker::Mimi,
                          QStringLiteral("こんにちは。ミミです。\n"
                                         "「ねえミミ」と呼んでください。"));
            say(QStringLiteral("こんにちは。ミミです。"));
        });
    } catch (const std::exception& e) {
        log::error(kTag, "voice startup failed: {}", e.what());
        status_->setText(QStringLiteral("no microphone"));
        chat_->append(Speaker::System,
                      QStringLiteral("Voice is offline: %1").arg(QString::fromUtf8(e.what())));
    }
}

// --------------------------------------------------------------------- state

void MainWindow::onState(int state) {
    orb_->setState(state);
    if (puck_ != nullptr) puck_->setState(state);

    const auto value = static_cast<voice::State>(state);
    const char* label = "listening";
    const char* colour = "#4dd8e6";
    switch (value) {
        case voice::State::Idle:      label = "listening";   colour = "#4dd8e6"; break;
        case voice::State::Listening: label = "hearing you"; colour = "#4dd8e6"; break;
        case voice::State::Thinking:  label = "thinking";    colour = "#f5c45e"; break;
        case voice::State::Speaking:  label = "speaking";    colour = "#58e28b"; break;
        case voice::State::Paused:    label = "muted";       colour = "#6b7396"; break;
    }
    status_->setText(QString::fromLatin1(label));
    if (statusDot_ != nullptr) {
        statusDot_->setStyleSheet(QStringLiteral("color:%1;")
                                      .arg(QString::fromLatin1(colour)));
    }

    // Button and status can never disagree: both are written here, once.
    if (power_ != nullptr) {
        const bool live = value != voice::State::Paused;
        QSignalBlocker blocker(power_);
        power_->setChecked(live);
        power_->setToolTip(live ? QStringLiteral("Mute the microphone")
                                : QStringLiteral("Unmute the microphone"));
    }
    if (micBadge_ != nullptr) micBadge_->setVisible(value != voice::State::Paused);
}

void MainWindow::onHeard(const QString& text, bool followUp) {
    clearSuggestions();
    chat_->append(Speaker::You, followUp ? text + QStringLiteral("  ↩") : text);
    respond(text);
}

void MainWindow::onSubmit() {
    const QString text = input_->text().trimmed();
    if (text.isEmpty()) return;
    input_->clear();
    clearSuggestions();
    chat_->append(Speaker::You, text);
    respond(text);
}

void MainWindow::onMicClicked() {
    if (!listener_) return;
    mic_->setEnabled(false);
    status_->setText(QStringLiteral("go ahead"));

    // capture_once() blocks until the endpointer closes the utterance, up to ten
    // seconds, so it cannot run on the GUI thread.
    std::thread([this] {
        const auto heard = listener_->capture_once(std::chrono::milliseconds{10000});
        const QString text = heard ? QString::fromStdString(*heard) : QString();
        QMetaObject::invokeMethod(this, [this, text] {
            mic_->setEnabled(true);
            if (text.isEmpty()) return;
            clearSuggestions();
            chat_->append(Speaker::You, text);
            respond(text);
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onListenToggled(bool listening) {
    if (!listener_) return;
    if (listening) {
        listener_->resume();
    } else {
        listener_->pause();
        if (speaker_) speaker_->stop();
    }
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

// -------------------------------------------------------------------- replies

void MainWindow::respond(const QString& prompt) {
    if (!router_) return;

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
    chat_->append(Speaker::Mimi, acted ? QStringLiteral("%1  ·  %2").arg(reply, action) : reply);
    say(reply);
}

void MainWindow::say(const QString& text) {
    if (!speaker_ || !listener_) return;
    listener_->set_speaking(true);
    speaker_->speak(text.toStdString(), [this](bool) {
        // AVFoundation and afplay both call back off the GUI thread.
        QMetaObject::invokeMethod(this, [this] {
            if (listener_) listener_->set_speaking(false);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Closing should not stop an assistant meant to be always on. The puck
    // stays, and Quit is in its menu.
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
