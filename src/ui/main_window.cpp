#include "ui/main_window.hpp"

#include "core/log.hpp"
#include "core/paths.hpp"
#include "ui/ambient.hpp"
#include "ui/command_bar.hpp"
#include "ui/controls.hpp"
#include "ui/mac_window.hpp"
#include "ui/theme.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <thread>

namespace mimi::ui {
namespace {

constexpr std::string_view kTag = "ui";
// Matches the unified-compact toolbar strip AppKit lays the traffic lights
// out in (~38pt), so the buttons sit optically centred against our controls.
constexpr int kTitleBarHeight = 40;

enum Page { PageHome = 0, PageSettings };

// The drag surface that replaces the hidden system title bar. Interactive
// children accept their own mouse events, so a press only lands here through
// the gaps -- exactly the regions that should move the window.
// startSystemMove() hands the drag to AppKit, which is what keeps window
// snapping, spaces and momentum identical to a native bar.
class TitleBar : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        armed_ = event->button() == Qt::LeftButton;
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (armed_ && (event->buttons() & Qt::LeftButton) &&
            window()->windowHandle() != nullptr) {
            armed_ = false;  // one native move per press
            window()->windowHandle()->startSystemMove();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        armed_ = false;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            armed_ = false;
            titlebar_double_clicked(this);
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

private:
    bool armed_ = false;
};

QWidget* placeholder(const QString& title, const QString& body) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(48, 40, 48, 40);
    layout->setSpacing(8);

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
    // Must precede winId(): translucency is baked into the NSWindow at
    // creation. This is what lets the vibrancy layer show through wherever
    // painting leaves alpha.
    setAttribute(Qt::WA_TranslucentBackground);
    resize(1120, 760);
    setMinimumSize(940, 660);

    // Layer 0: the ambient canvas. Everything else floats over it.
    auto* root = new AmbientCanvas;
    root->setObjectName(QStringLiteral("root"));

    auto* column = new QVBoxLayout(root);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(buildTitleBar());

    pages_ = new QStackedWidget;
    home_ = new HomeView;
    settings_ = placeholder(
        QStringLiteral("SETTINGS"),
        QStringLiteral("Voice, model and wake-word settings will live here."));

    pages_->addWidget(home_);
    pages_->addWidget(settings_);
    column->addWidget(pages_, 1);

    // Layer 2: the command bar, floating clear of every edge on its shadow.
    composer_ = new CommandBar;
    composer_->setMaximumWidth(640);
    connect(composer_, &CommandBar::submitted, this, &MainWindow::ask);
    connect(composer_, &CommandBar::micClicked, this, &MainWindow::onMicClicked);

    auto* dock = new QWidget;
    auto* dockRow = new QHBoxLayout(dock);
    dockRow->setContentsMargins(48, 4, 48, 26);
    dockRow->addStretch(1);
    dockRow->addWidget(composer_, 8);
    dockRow->addStretch(1);
    column->addWidget(dock);

    setCentralWidget(root);

    connect(home_, &HomeView::commandRequested, this, &MainWindow::ask);

    puck_ = new FloatingOrb;
    puck_->moveToDefaultCorner();
    puck_->show();
    connect(puck_, &FloatingOrb::clicked, this, &MainWindow::toggleWindow);
    connect(puck_, &FloatingOrb::doubleClicked, this, &MainWindow::onMicClicked);
    connect(puck_, &FloatingOrb::muteRequested, this, &MainWindow::setMuted);
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
    add_window_vibrancy(this);
    if (auto* spacer = findChild<QWidget*>(QStringLiteral("trafficLights"))) {
        spacer->setFixedWidth(traffic_light_inset(this));
    }
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    // After the event settles: everything in applyNativeChrome is idempotent,
    // and the traffic-light inset can only be measured once AppKit has laid
    // the buttons out for the current toolbar style.
    QTimer::singleShot(0, this, [this] { applyNativeChrome(); });
}

// ----------------------------------------------------------------- title bar

QWidget* MainWindow::buildTitleBar() {
    auto* bar = new TitleBar;
    bar->setObjectName(QStringLiteral("titleBar"));
    bar->setFixedHeight(kTitleBarHeight);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 12, 0);
    layout->setSpacing(4);

    // Reserved for the traffic lights, which float over the content once the
    // window uses a full-size content view.
    auto* lights = new QWidget;
    lights->setObjectName(QStringLiteral("trafficLights"));
    lights->setFixedWidth(78);
    layout->addWidget(lights);

    auto* name = new QLabel(QStringLiteral("Mimi"));
    name->setObjectName(QStringLiteral("wordmark"));
    layout->addWidget(name);

    // The middle of the bar is empty on purpose: it is the drag surface,
    // exactly like a native title bar.
    layout->addStretch(1);

    // One status capsule instead of a scatter of badges: a dot and a word.
    auto* pill = new QWidget;
    pill->setObjectName(QStringLiteral("statusPill"));
    auto* pillRow = new QHBoxLayout(pill);
    pillRow->setContentsMargins(12, 4, 12, 4);
    pillRow->setSpacing(7);

    statusDot_ = new QLabel(QStringLiteral("●"));
    statusDot_->setObjectName(QStringLiteral("statusDot"));
    pillRow->addWidget(statusDot_);

    statusText_ = new QLabel(QStringLiteral("Starting"));
    statusText_->setObjectName(QStringLiteral("statusText"));
    pillRow->addWidget(statusText_);
    layout->addWidget(pill);

    layout->addSpacing(8);

    // Voice controls, in full: the talk button, and mute spelled out as a
    // proper labelled control next to it rather than a mystery icon.
    talkBtn_ = new GhostButton(icons::Glyph::Mic);
    talkBtn_->setToolTip(QStringLiteral("Speak now — no wake word needed"));
    connect(talkBtn_, &GhostButton::clicked, this, &MainWindow::onMicClicked);
    layout->addWidget(talkBtn_);

    mutePill_ = new QPushButton(QStringLiteral("Mute"));
    mutePill_->setObjectName(QStringLiteral("mutePill"));
    mutePill_->setCheckable(true);
    mutePill_->setCursor(Qt::PointingHandCursor);
    mutePill_->setFixedHeight(30);
    mutePill_->setToolTip(QStringLiteral("Stop listening"));
    connect(mutePill_, &QPushButton::toggled, this, [this](bool muted) {
        setMuted(muted);
        mutePill_->setText(muted ? QStringLiteral("Muted") : QStringLiteral("Mute"));
    });
    layout->addWidget(mutePill_);

    layout->addSpacing(2);

    settingsBtn_ = new GhostButton(icons::Glyph::Settings);
    settingsBtn_->setCheckable(true);
    settingsBtn_->setToolTip(QStringLiteral("Settings"));
    connect(settingsBtn_, &GhostButton::clicked, this, [this] {
        navigate(pages_->currentIndex() == PageSettings ? PageHome : PageSettings);
    });
    layout->addWidget(settingsBtn_);

    return bar;
}

void MainWindow::navigate(int page) {
    pages_->setCurrentIndex(page);
    settingsBtn_->setChecked(page == PageSettings);
}

void MainWindow::setMuted(bool muted) {
    if (!listener_) return;
    if (muted) {
        listener_->pause();
        if (speaker_) speaker_->stop();
    } else {
        listener_->resume();
    }
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
                                                  "Say “hey mimi” whenever you need me."));
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
}

// --------------------------------------------------------------------- state

void MainWindow::onState(int state) {
    home_->setState(state);
    if (puck_ != nullptr) puck_->setState(state);

    const auto value = static_cast<voice::State>(state);
    const char* label = "Ready";
    QColor colour = theme::kAccentDeep;
    switch (value) {
        case voice::State::Idle:      label = "Ready";     colour = theme::kAccentDeep; break;
        case voice::State::Listening: label = "Listening"; colour = theme::kAccent;     break;
        case voice::State::Thinking:  label = "Thinking";  colour = theme::kAccentSoft; break;
        case voice::State::Speaking:  label = "Speaking";  colour = theme::kInk;        break;
        case voice::State::Paused:    label = "Muted";     colour = theme::kFaint;      break;
    }
    statusText_->setText(QString::fromLatin1(label));
    statusDot_->setStyleSheet(QStringLiteral("color:%1;").arg(colour.name()));

    const bool live = value != voice::State::Paused;
    if (mutePill_ != nullptr) {
        const bool blocked = mutePill_->blockSignals(true);
        mutePill_->setChecked(!live);
        mutePill_->blockSignals(blocked);
        mutePill_->setText(live ? QStringLiteral("Mute") : QStringLiteral("Muted"));
        mutePill_->setToolTip(live ? QStringLiteral("Stop listening")
                                   : QStringLiteral("Muted — click to resume"));
    }
}

// -------------------------------------------------------------------- input

void MainWindow::onHeard(const QString& text, bool followUp) {
    Q_UNUSED(followUp);
    ask(text);
}

void MainWindow::onMicClicked() {
    if (!listener_) return;
    composer_->setMicEnabled(false);

    // capture_once() blocks until the endpointer closes the utterance, so it
    // cannot run on the GUI thread.
    std::thread([this] {
        const auto heard = listener_->capture_once(std::chrono::milliseconds{10000});
        const QString text = heard ? QString::fromStdString(*heard) : QString();
        QMetaObject::invokeMethod(this, [this, text] {
            composer_->setMicEnabled(true);
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
    Q_UNUSED(action);
    Q_UNUSED(acted);
    home_->setExchange(pending_, reply);
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
