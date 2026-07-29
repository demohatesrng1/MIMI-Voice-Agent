#include "ui/main_window.hpp"

#include "core/log.hpp"
#include "core/paths.hpp"
#include "ui/icons.hpp"
#include "ui/mac_window.hpp"
#include "ui/theme.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <thread>

namespace mimi::ui {
namespace {

constexpr std::string_view kTag = "ui";
constexpr int kTitleBarHeight = 44;

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

QWidget* placeholder(const QString& title, const QString& body) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 26, 30, 26);
    layout->setSpacing(6);

    auto* heading = new QLabel(title);
    heading->setObjectName(QStringLiteral("sectionHead"));
    layout->addWidget(heading);

    auto* text = new QLabel(body);
    text->setObjectName(QStringLiteral("sectionLead"));
    text->setWordWrap(true);
    layout->addWidget(text);

    layout->addStretch(1);
    return page;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Mimi"));
    resize(1120, 760);
    setMinimumSize(940, 660);

    auto* root = new QWidget;
    root->setObjectName(QStringLiteral("root"));

    auto* column = new QVBoxLayout(root);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(buildTitleBar());
    column->addWidget(hairline(Qt::Horizontal));

    auto* body = new QWidget;
    auto* row = new QHBoxLayout(body);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    rail_ = new NavRail;
    row->addWidget(rail_);
    row->addWidget(hairline(Qt::Vertical));

    // The pages, plus one composer shared across all of them. Typing should
    // work wherever you happen to be, rather than only on a "chat" tab.
    auto* right = new QWidget;
    auto* rightColumn = new QVBoxLayout(right);
    rightColumn->setContentsMargins(0, 0, 0, 0);
    rightColumn->setSpacing(0);

    pages_ = new QStackedWidget;
    home_ = new HomeView;
    activity_ = new ActivityView;
    skills_ = new SkillsView;
    settings_ = placeholder(
        QStringLiteral("SETTINGS"),
        QStringLiteral("Voice, model and wake-word settings will live here."));

    pages_->addWidget(home_);
    pages_->addWidget(activity_);
    pages_->addWidget(skills_);
    pages_->addWidget(settings_);
    rightColumn->addWidget(pages_, 1);
    row->addWidget(right, 1);

    column->addWidget(body, 1);
    setCentralWidget(root);

    connect(rail_, &NavRail::pageSelected, this, [this](int page) {
        pages_->setCurrentIndex(page);
    });
    connect(rail_, &NavRail::muteToggled, this, [this](bool muted) {
        if (!listener_) return;
        if (muted) {
            listener_->pause();
            if (speaker_) speaker_->stop();
        } else {
            listener_->resume();
        }
    });
    connect(home_, &HomeView::commandRequested, this, &MainWindow::ask);
    connect(skills_, &SkillsView::commandRequested, this, [this](const QString& utterance) {
        rail_->setCurrent(NavRail::Home);
        pages_->setCurrentIndex(NavRail::Home);
        ask(utterance);
    });

    puck_ = new FloatingOrb;
    puck_->moveToDefaultCorner();
    puck_->show();
    connect(puck_, &FloatingOrb::clicked, this, &MainWindow::toggleWindow);
    connect(puck_, &FloatingOrb::doubleClicked, this, &MainWindow::onMicClicked);
    connect(puck_, &FloatingOrb::muteRequested, this, [this](bool muted) {
        if (!listener_) return;
        if (muted) {
            listener_->pause();
        } else {
            listener_->resume();
        }
    });
    connect(puck_, &FloatingOrb::quitRequested, qApp, &QApplication::quit);

    bridge_ = new VoiceBridge(this);
    connect(bridge_, &VoiceBridge::stateChanged, this, &MainWindow::onState);
    connect(bridge_, &VoiceBridge::levelChanged, this, [this](float rms, float) {
        home_->setLevel(rms);
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
    layout->setContentsMargins(0, 0, 14, 0);
    layout->setSpacing(0);

    // Reserved for the traffic lights, which float over the content once the
    // window uses a full-size content view.
    auto* lights = new QWidget;
    lights->setObjectName(QStringLiteral("trafficLights"));
    lights->setFixedWidth(78);
    layout->addWidget(lights);

    auto* name = new QLabel(QStringLiteral("MIMI"));
    name->setObjectName(QStringLiteral("wordmark"));
    layout->addWidget(name);

    layout->addStretch(1);

    // The command field lives here, centred, the way VS Code puts its search
    // in the title bar. It gives the bar a job -- an empty strip with a label
    // on each end is wasted chrome -- and it is reachable from every page.
    input_ = new QLineEdit;
    input_->setObjectName(QStringLiteral("omni"));
    input_->setPlaceholderText(QStringLiteral("Ask Mimi, or type a command"));
    input_->setFixedWidth(420);
    input_->setAlignment(Qt::AlignCenter);
    input_->setClearButtonEnabled(false);
    // Placeholder colour comes from the palette, not the stylesheet: QSS has no
    // selector that reaches it, so styling it in the .qss silently does nothing.
    {
        QPalette palette = input_->palette();
        palette.setColor(QPalette::PlaceholderText, QColor(0x5a, 0x5a, 0x70));
        input_->setPalette(palette);
    }
    connect(input_, &QLineEdit::returnPressed, this, &MainWindow::onSubmit);
    layout->addWidget(input_);

    mic_ = new QPushButton;
    mic_->setObjectName(QStringLiteral("micInline"));
    mic_->setFixedSize(28, 28);
    mic_->setIconSize(QSize(16, 16));
    mic_->setIcon(icons::icon(icons::Glyph::Mic, QColor(0x87, 0x87, 0x9c), 16));
    mic_->setCursor(Qt::PointingHandCursor);
    mic_->setToolTip(QStringLiteral("Speak now, no wake word needed"));
    connect(mic_, &QPushButton::clicked, this, &MainWindow::onMicClicked);
    layout->addSpacing(6);
    layout->addWidget(mic_);

    layout->addStretch(1);

    micBadge_ = new QLabel(QStringLiteral("REC"));
    micBadge_->setObjectName(QStringLiteral("micBadge"));
    micBadge_->setToolTip(QStringLiteral("The microphone is open"));
    layout->addWidget(micBadge_);
    layout->addSpacing(12);

    statusDot_ = new QLabel(QStringLiteral("\u25cf"));
    statusDot_->setObjectName(QStringLiteral("statusDot"));
    layout->addWidget(statusDot_);
    layout->addSpacing(7);

    statusText_ = new QLabel(QStringLiteral("starting"));
    statusText_->setObjectName(QStringLiteral("statusText"));
    statusText_->setMinimumWidth(74);
    layout->addWidget(statusText_);

    return bar;
}

// ------------------------------------------------------------------- startup

void MainWindow::startVoice() {
    const auto& models = paths::models_dir();

    voice::Listener::Config config;
    config.wake_backend = voice::Listener::WakeBackend::PhraseSpotter;
    config.vad_model = models / "silero_vad.onnx";
    config.whisper_model = models / "ggml-small.bin";
    config.language = "ja";
    config.gate_model.clear();

    try {
        ollama_ = std::make_unique<brain::Ollama>(brain::Ollama::Config{});
        router_ = std::make_unique<brain::Router>(*ollama_);
        router_->on_reminder([this](const std::string& text) {
            const QString message = QString::fromStdString(text);
            QMetaObject::invokeMethod(this, [this, message] {
                home_->setExchange(QString(), message);
                activity_->record(QStringLiteral("Reminder"), message,
                                  QStringLiteral("reminder"), true);
                say(message);
            }, Qt::QueuedConnection);
        });

        capture_ = std::make_unique<audio::Capture>();
        capture_->start();

        speaker_ = std::make_unique<voice::Speaker>(voice::Speaker::Config{});
        listener_ = std::make_unique<voice::Listener>(*capture_, std::move(config));
        bridge_->attach(*listener_);

        QTimer::singleShot(0, this, [this] {
            const bool brain_up = ollama_->ensure_running();
            const bool model_ok = brain_up && ollama_->model_available();
            if (model_ok) ollama_->warmup();
            if (speaker_ && !speaker_->using_voicevox()) speaker_->start_voicevox();

            listener_->warmup();
            listener_->start();

            if (!brain_up) {
                note(QStringLiteral("Could not start Ollama"));
            } else if (!model_ok) {
                note(QStringLiteral("Model %1 is not installed")
                         .arg(QString::fromStdString(ollama_->config().model)));
            } else {
                home_->setExchange(QString(),
                                   QStringLiteral("Hello. I'm Mimi.\n"
                                                  "Say \u201chey mimi\u201d whenever you need me."));
                say(QStringLiteral("こんにちは。ミミです。"));
            }
        });
    } catch (const std::exception& e) {
        // Report what actually failed. Every startup exception used to be
        // announced as a microphone problem, which sent me hunting the wrong
        // thing when the real fault was a missing model file.
        log::error(kTag, "voice startup failed: {}", e.what());
        note(QStringLiteral("Startup failed: %1").arg(QString::fromUtf8(e.what())));
    }
}

void MainWindow::note(const QString& message) {
    home_->setExchange(QString(), message);
    activity_->record(QStringLiteral("System"), message, QString(), false);
}

// --------------------------------------------------------------------- state

void MainWindow::onState(int state) {
    home_->setState(state);
    if (puck_ != nullptr) puck_->setState(state);

    const auto value = static_cast<voice::State>(state);
    const char* label = "listening";
    const char* colour = "#ff2d87";
    switch (value) {
        case voice::State::Idle:      label = "listening";   colour = "#9e1853"; break;
        case voice::State::Listening: label = "hearing you"; colour = "#ff2d87"; break;
        case voice::State::Thinking:  label = "thinking";    colour = "#ff7ab4"; break;
        case voice::State::Speaking:  label = "speaking";    colour = "#f3f3f8"; break;
        case voice::State::Paused:    label = "muted";       colour = "#4d4d60"; break;
    }
    statusText_->setText(QString::fromLatin1(label));
    statusDot_->setStyleSheet(QStringLiteral("color:%1;").arg(QString::fromLatin1(colour)));

    const bool live = value != voice::State::Paused;
    if (micBadge_ != nullptr) micBadge_->setVisible(live);
    if (rail_ != nullptr) rail_->setListening(live);
}

// -------------------------------------------------------------------- input

void MainWindow::onHeard(const QString& text, bool followUp) {
    Q_UNUSED(followUp);
    ask(text);
}

void MainWindow::onSubmit() {
    const QString text = input_->text().trimmed();
    if (text.isEmpty()) return;
    input_->clear();
    ask(text);
}

void MainWindow::onMicClicked() {
    if (!listener_) return;
    mic_->setEnabled(false);

    // capture_once() blocks until the endpointer closes the utterance, so it
    // cannot run on the GUI thread.
    std::thread([this] {
        const auto heard = listener_->capture_once(std::chrono::milliseconds{10000});
        const QString text = heard ? QString::fromStdString(*heard) : QString();
        QMetaObject::invokeMethod(this, [this, text] {
            mic_->setEnabled(true);
            if (!text.isEmpty()) ask(text);
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::ask(const QString& utterance) {
    if (!router_ || utterance.isEmpty()) return;

    pending_ = utterance;
    home_->setExchange(utterance, QString());
    home_->setThinking();

    const std::string text = utterance.toStdString();
    std::thread([this, text] {
        const auto reply = router_->route(text);
        const QString replied = QString::fromStdString(reply.text);
        const QString action = QString::fromStdString(reply.action);
        const bool acted = reply.acted;
        QMetaObject::invokeMethod(this, [this, replied, action, acted] {
            deliver(replied, action, acted);
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::deliver(const QString& reply, const QString& action, bool acted) {
    home_->setExchange(pending_, reply);
    activity_->record(pending_, reply, action, acted);
    say(reply);
}

void MainWindow::say(const QString& text) {
    if (!speaker_ || !listener_) return;
    listener_->set_speaking(true);
    speaker_->speak(text.toStdString(), [this](bool) {
        QMetaObject::invokeMethod(this, [this] {
            if (listener_) listener_->set_speaking(false);
        }, Qt::QueuedConnection);
    });
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
