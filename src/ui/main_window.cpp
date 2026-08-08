#include "ui/main_window.hpp"

#include "ui/notes_view.hpp"

#include "core/log.hpp"
#include "core/paths.hpp"
#include "ui/ambient.hpp"
#ifdef MIMI_HAS_AVATAR
#include "ui/avatar_view.hpp"
#endif
#include "ui/command_bar.hpp"
#include "ui/command_palette.hpp"
#include "brain/accessibility.hpp"
#include "brain/journal.hpp"
#include "brain/notes.hpp"
#include "brain/tools.hpp"
#include "ui/context_ribbon.hpp"
#include "ui/faces.hpp"
#include "ui/controls.hpp"
#include "ui/settings_view.hpp"
#include "ui/icons.hpp"
#include "ui/mac_window.hpp"
#include "ui/neural_search.hpp"
#include "ui/presence.hpp"
#include "ui/timeline_view.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <thread>

namespace mimi::ui {
namespace {

constexpr std::string_view kTag = "ui";
// Matches the unified-compact toolbar strip AppKit lays the traffic lights
// out in (~38pt), so the buttons sit optically centred against our controls.
constexpr int kTitleBarHeight = 40;

// Only surfaces backed by something real.
//
// Canvas, Missions and Relationships were cut: each was a page of invented
// content -- a client called Acme Corp, a Q3 proposal, a launch plan -- with no
// way to put anything true into it. A page that can only ever show a fixture is
// a screenshot, not a feature. The classes are still in the tree for when they
// have a real source.
enum Page { PageHome = 0, PageNotes, PageTimeline, PageSettings };

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

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Mimi"));
    // Must precede winId(): translucency is baked into the NSWindow at
    // creation. This is what lets the vibrancy layer show through wherever
    // painting leaves alpha.
    setAttribute(Qt::WA_TranslucentBackground);
    // Two sizes and no third.
    //
    // The layout is composed for one shape, so a freely resizable window buys
    // nothing except a hundred shapes it was never designed at. It opens large
    // and fixed, and the only other state is full screen -- which is the one
    // people actually want for something they leave open all day.
    applyFixedSize();

    // Layer 0: the living background. Everything else floats over it, and it
    // reflects her presence, so the whole room changes with what she is doing.
    ambient_ = new AmbientCanvas;
    ambient_->setObjectName(QStringLiteral("root"));
    auto* root = ambient_;

    auto* column = new QVBoxLayout(root);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(buildTitleBar());

    // The context ribbon: what she understands the work to be, always on show
    // just under the chrome.

    // The ribbon reports what is actually true right now: the app she would act
    // on if you asked, and how many notes she is holding. Polled, because macOS
    // has no notification for "the frontmost app changed" that does not require
    // observing every process.
    contextTimer_ = new QTimer(this);
    contextTimer_->setInterval(2000);
    connect(contextTimer_, &QTimer::timeout, this, &MainWindow::refreshContext);
    contextTimer_->start();

    pages_ = new QStackedWidget;
    home_ = new HomeView;
    notes_ = new NotesView;
    timeline_ = new TimelineView;
    settings_ = new SettingsView;

    // Order matters: it is the Page enum.
    pages_->addWidget(home_);
    pages_->addWidget(notes_);
    pages_->addWidget(timeline_);
    pages_->addWidget(settings_);
    column->addWidget(pages_, 1);

    // The command bar is no longer here: it belongs to her half of the command
    // centre, at the foot of the left column, so the thing you type into sits
    // under the thing you are talking to rather than across the whole window.

    // The context strip lives at the foot, as a status bar. Directly under the
    // title bar it was a second band of chrome saying very little -- two rows
    // of furniture before any content, which is the thing an editor never does.
    // At the bottom it reads as status, which is what it is.
    // The foot strip is gone too: everything it reported -- notes, control --
    // is on the right-hand panel now, where it is read rather than skimmed.
    ribbon_ = nullptr;

    // Old journal days go at startup, so the log has a bounded life without
    // anyone having to remember to clear it.
    brain::Journal().prune(90);

    // Reminders set before the last quit. Anything already due fires straight
    // away, which is how a reminder survives the app being closed at all.
    brain::tools::restore_reminders([this](const std::string& text) {
        const std::string spoken = brain::tools::reminder_announcement(text);
        brain::tools::notify("ミミ", "リマインダー", spoken);
        brain::tools::play_notification_cue();
        QMetaObject::invokeMethod(this, [this, spoken] {
            say(QString::fromStdString(spoken));
        }, Qt::QueuedConnection);
    });

    setCentralWidget(root);

    connect(home_, &HomeView::commandRequested, this, &MainWindow::ask);
    // Holding the orb, or pressing Voice Mode, both mean the same thing: start
    // listening now, without the keyboard.
    connect(home_, &HomeView::voiceRequested, this, &MainWindow::onMicClicked);

    // Floating overlays, parented to the ambient root so they hover over the
    // pages. Positioned by layoutOverlays(), not the column layout.
    palette_ = new CommandPalette(ambient_);
    connect(palette_, &CommandPalette::commandChosen, this, &MainWindow::ask);
    connect(palette_, &CommandPalette::navigateChosen, this, &MainWindow::navigate);

    search_ = new NeuralSearch(ambient_);
    connect(search_, &NeuralSearch::navigateChosen, this, &MainWindow::navigate);

    auto* paletteKey = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    connect(paletteKey, &QShortcut::activated, this, [this] { palette_->open(); });
    // The macOS full-screen key, and Escape to come back out of it.
    auto* fullKey = new QShortcut(QKeySequence(Qt::CTRL | Qt::META | Qt::Key_F), this);
    connect(fullKey, &QShortcut::activated, this, &MainWindow::toggleFullScreen);
    auto* leaveFull = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(leaveFull, &QShortcut::activated, this, [this] {
        if (isFullScreen()) toggleFullScreen();
    });

    auto* searchKey = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(searchKey, &QShortcut::activated, this, [this] { search_->open(); });

    puck_ = new FloatingOrb;
    puck_->moveToDefaultCorner();
    puck_->show();
    connect(puck_, &FloatingOrb::clicked, this, &MainWindow::toggleWindow);
    connect(puck_, &FloatingOrb::doubleClicked, this, &MainWindow::onMicClicked);
    connect(puck_, &FloatingOrb::muteRequested, this, &MainWindow::setMuted);
    connect(puck_, &FloatingOrb::quitRequested, qApp, &QApplication::quit);
    // The orb is meant to be always there. Switching to another application is
    // exactly when macOS would order a panel out, so re-assert it then -- and
    // show it again if anything managed to hide it.
    connect(qApp, &QApplication::applicationStateChanged, puck_, [this] {
        if (puck_ == nullptr) return;
        if (!puck_->isVisible()) puck_->show();
        puck_->pinToAllSpaces();
    });

    // She files an exchange away when it finishes: this holds the "Updating
    // memory" beat before the real voice state resumes.
    rememberTimer_ = new QTimer(this);
    rememberTimer_->setSingleShot(true);
    connect(rememberTimer_, &QTimer::timeout, this, [this] {
        remembering_ = false;
        applyPresence(presence_for(voiceState_));
    });

    bridge_ = new VoiceBridge(this);
    connect(bridge_, &VoiceBridge::stateChanged, this, &MainWindow::onState);
    connect(bridge_, &VoiceBridge::levelChanged, this, [this](float rms, float) {
        if (rms > 0.0f) heardAnything_ = true;
        home_->setLevel(rms);
        puck_->setLevel(rms);
        ambient_->setLevel(rms);
    });
    connect(bridge_, &VoiceBridge::heard, this, &MainWindow::onHeard);
    connect(bridge_, &VoiceBridge::bargedIn, this, [this] {
        if (speaker_) speaker_->stop();
        // The audio is gone; her mouth has to stop with it, or she carries on
        // silently mouthing the rest of an answer nobody can hear.
#ifdef MIMI_HAS_AVATAR
        if (auto* avatar = home_->avatar()) avatar->clearVisemes();
#endif
    });

    navigate(PageHome);
}

MainWindow::~MainWindow() {
    if (listener_) listener_->stop();
    if (capture_) capture_->stop();
    delete puck_;
}

void MainWindow::applyNativeChrome() {
    // Every call below reaches through to an NSWindow. Under the offscreen
    // platform plugin there is not one, and asking for it takes the process
    // down -- which is why the whole window could never be rendered for review
    // and only its individual pages could.
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    adopt_native_titlebar(this);
    add_window_vibrancy(this);
    if (auto* spacer = findChild<QWidget*>(QStringLiteral("trafficLights"))) {
        spacer->setFixedWidth(traffic_light_inset(this));
    }
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    layoutOverlays();
    // After the event settles: everything in applyNativeChrome is idempotent,
    // and the traffic-light inset can only be measured once AppKit has laid
    // the buttons out for the current toolbar style.
    QTimer::singleShot(0, this, [this] { applyNativeChrome(); });
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    layoutOverlays();
}

// The floating overlays are children of the ambient root, not the column
// layout, so they are placed by hand whenever the window changes size.
void MainWindow::refreshContext() {
    if (ribbon_ == nullptr) return;
    const int notes = static_cast<int>(brain::Notes().all().size());
    ribbon_->setMetric(QStringLiteral("NOTES"), QString::number(notes));
    ribbon_->setMetric(QStringLiteral("CONTROL"),
                       brain::ax::has_permission() ? QStringLiteral("On")
                                                   : QStringLiteral("Off"));
}

void MainWindow::layoutOverlays() {
    if (ambient_ == nullptr) return;
    if (palette_ != nullptr) palette_->setGeometry(ambient_->rect());
    if (search_ != nullptr) search_->setGeometry(ambient_->rect());
}

// ----------------------------------------------------------------- title bar

// A breathing presence dot. The only green in the interface, and it means one
// thing: she is here. Shares the orb's 3-second breath, so the two read as one
// heartbeat rather than two timers.
class PresenceDot : public QWidget {
public:
    explicit PresenceDot(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(14, 14);
        auto* clock = new QTimer(this);
        clock->setInterval(40);
        connect(clock, &QTimer::timeout, this, [this] {
            phase_ = std::fmod(phase_ + 0.04 / 3.0, 1.0);
            update();
        });
        clock->start();
    }

    void setLive(bool live) { live_ = live; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPointF centre(width() / 2.0, height() / 2.0);
        const double breath = 0.5 - 0.5 * std::cos(phase_ * 2 * M_PI);
        QColor colour = live_ ? theme::kLive : theme::kFaint;

        if (live_) {
            QColor halo = colour;
            halo.setAlphaF(0.34 * (1.0 - breath));
            painter.setPen(Qt::NoPen);
            painter.setBrush(halo);
            painter.drawEllipse(centre, 3.0 + breath * 4.0, 3.0 + breath * 4.0);
        }
        colour.setAlphaF(live_ ? 0.55 + 0.45 * breath : 0.5);
        painter.setBrush(colour);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(centre, 3.0, 3.0);
    }

private:
    double phase_ = 0.0;
    bool live_ = true;
};

// Two clusters and nothing between them.
//
// There is no centre element in the bar at all -- not an empty one, not a
// spacer. A stretch does the separating, so there is nothing to leave unused
// and nothing to collapse awkwardly at a small width. Everything that used to
// live here (the pages, Voice, Mute) is reached from the gear or the command
// palette: on a screen meant to be a sanctuary, five controls in the chrome is
// four too many.
// The one fixed shape, as large as the display comfortably allows.
void MainWindow::applyFixedSize() {
    QSize wanted(1440, 940);
    if (QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        const QRect avail = screen->availableGeometry();
        wanted.setWidth(std::min(wanted.width(), avail.width() - 60));
        wanted.setHeight(std::min(wanted.height(), avail.height() - 60));
    }
    setFixedSize(wanted);
}

// Full screen is the only other size. Coming back has to lift the fixed-size
// constraint first, or Qt refuses to grow the window at all.
void MainWindow::toggleFullScreen() {
    if (isFullScreen()) {
        showNormal();
        applyFixedSize();
        return;
    }
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    showFullScreen();
}

QWidget* MainWindow::buildTitleBar() {
    auto* bar = new TitleBar;
    bar->setObjectName(QStringLiteral("titleBar"));
    bar->setFixedHeight(kTitleBarHeight);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 16, 0);
    layout->setSpacing(0);

    // Reserved for the traffic lights, which float over the content once the
    // window uses a full-size content view.
    auto* lights = new QWidget;
    lights->setObjectName(QStringLiteral("trafficLights"));
    lights->setFixedWidth(78);
    layout->addWidget(lights);

    auto* name = new QLabel(QStringLiteral("Mimi"));
    name->setObjectName(QStringLiteral("wordmark"));
    layout->addWidget(name);
    layout->addSpacing(9);

    statusDot_ = nullptr;
    auto* dot = new PresenceDot;
    presenceDot_ = dot;
    layout->addWidget(dot);

    layout->addStretch(1);

    // The gear, and her face. These are the only two.
    settingsBtn_ = new GhostButton(icons::Glyph::Settings);
    settingsBtn_->setToolTip(QStringLiteral("Settings"));
    connect(settingsBtn_, &QPushButton::clicked, this,
            [this] { navigate(PageSettings); });
    layout->addWidget(settingsBtn_);
    layout->addSpacing(6);

    // Her portrait doubles as the way back to the home screen -- tapping the
    // person you are talking to is the most obvious "take me to her" there is.
    auto* avatar = new QPushButton;
    avatar->setObjectName(QStringLiteral("barAvatar"));
    avatar->setFixedSize(30, 30);
    avatar->setCursor(Qt::PointingHandCursor);
    avatar->setIcon(QIcon(faces::current(60)));
    avatar->setIconSize(QSize(30, 30));
    avatar->setFlat(true);
    avatar->setToolTip(QStringLiteral("Home"));
    connect(avatar, &QPushButton::clicked, this, [this] { navigate(PageHome); });
    layout->addWidget(avatar);

    // Kept alive so the rest of MainWindow can still speak to them, but off the
    // bar: the status line is the dot now, and Stop appears only while she talks.
    statusText_ = new QLabel;
    statusText_->hide();
    stopBtn_ = new QPushButton(QStringLiteral("Stop"));
    stopBtn_->setObjectName(QStringLiteral("stopBtn"));
    stopBtn_->setVisible(false);
    connect(stopBtn_, &QPushButton::clicked, this, [this] {
        if (speaker_) speaker_->stop();
        if (listener_) listener_->set_speaking(false);
#ifdef MIMI_HAS_AVATAR
        if (auto* view = home_->avatar()) view->clearVisemes();
#endif
    });
    layout->addWidget(stopBtn_);

    return bar;
}

void MainWindow::navigate(int page) {
    pages_->setCurrentIndex(page);
    if (navHome_ != nullptr) navHome_->setChecked(page == PageHome);
    if (navTimeline_ != nullptr) navTimeline_->setChecked(page == PageTimeline);
    if (navNotes_ != nullptr) navNotes_->setChecked(page == PageNotes);
    // The voice path writes notes straight to disk, so re-read on the way in.
    if (page == PageNotes && notes_ != nullptr) notes_->refresh();
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
                timeline_->remember(QString(), message);
                say(message);
            }, Qt::QueuedConnection);
        });

        capture_ = std::make_unique<audio::Capture>();
        capture_->start();

        // Say so when she cannot hear.
        //
        // A denied microphone does not fail: CoreAudio opens the device, runs
        // the callback and hands over a stream of exact zeros for ever. Every
        // layer above behaves perfectly on silence -- the VAD finds no speech,
        // the spotter is never asked, and she waits, apparently working, for a
        // wake word that can never arrive. It is indistinguishable from a bug
        // unless something checks, so this checks.
        if (brain::ax::microphone_access() != brain::ax::Access::Granted) {
            note(QStringLiteral("Mimi cannot hear you — microphone access is off"));
            log::warn(kTag, "microphone access is not granted; she will hear only silence");
            brain::ax::open_microphone_settings();
        }

        speaker_ = std::make_unique<voice::Speaker>(voice::Speaker::Config{});
#ifdef MIMI_HAS_AVATAR
        // Lip sync. The timeline is the synthesiser's own prosody plan, so her
        // mouth is right by construction rather than chasing the waveform.
        // Fired from whichever thread started playback, hence the hop to the
        // GUI thread before anything touches a widget.
        speaker_->on_visemes([this](const std::vector<voice::Mora>& timeline, double delay) {
            auto* avatar = home_ != nullptr ? home_->avatar() : nullptr;
            if (avatar == nullptr) return;
            QVector<AvatarView::Mora> track;
            track.reserve(static_cast<int>(timeline.size()));
            for (const voice::Mora& mora : timeline) {
                track.append(AvatarView::Mora{mora.t, mora.length, mora.vowel});
            }
            QMetaObject::invokeMethod(
                avatar, [avatar, track, delay] { avatar->playVisemes(track, delay); },
                Qt::QueuedConnection);
        });
#endif
        listener_ = std::make_unique<voice::Listener>(*capture_, std::move(config));
        bridge_->attach(*listener_);

        QTimer::singleShot(0, this, [this] {
            const bool brain_up = ollama_->ensure_running();
            const bool model_ok = brain_up && ollama_->model_available();
            if (model_ok) ollama_->warmup();
            if (speaker_ && !speaker_->using_voicevox()) speaker_->start_voicevox();

            listener_->warmup();
            listener_->start();

            // Even a silent room is not digitally silent. If nothing but exact
            // zeros has arrived after ten seconds of listening, the microphone
            // is muted, missing or blocked -- and she would otherwise sit there
            // looking attentive for ever.
            QTimer::singleShot(10000, this, [this] {
                if (heardAnything_ || listener_ == nullptr) return;
                note(QStringLiteral("Silence on the microphone — she cannot hear you"));
                log::warn(kTag, "no non-zero audio in 10s: input is muted or blocked");
            });

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
    voiceState_ = static_cast<voice::State>(state);
    if (puck_ != nullptr) puck_->setState(state);

    // Remembering is a brief overlay raised when an answer finishes; let it run
    // its course before the live voice state repaints the room.
    if (!remembering_) applyPresence(presence_for(voiceState_));

    if (stopBtn_ != nullptr) {
        stopBtn_->setVisible(voiceState_ == voice::State::Speaking);
    }

    const bool live = voiceState_ != voice::State::Paused;
    if (mutePill_ != nullptr) {
        const bool blocked = mutePill_->blockSignals(true);
        mutePill_->setChecked(!live);
        mutePill_->blockSignals(blocked);
        mutePill_->setText(live ? QStringLiteral("Mute") : QStringLiteral("Unmute"));
        mutePill_->setToolTip(live ? QStringLiteral("Stop listening")
                                   : QStringLiteral("Start listening again"));
    }
}

// One presence, fanned out to every surface at once.
void MainWindow::applyPresence(Presence presence) {
    home_->setPresence(presence);
    ambient_->setPresence(presence);
    audio_.cue(presence);  // ambient audio: a soft cue on each state change
    if (statusText_ != nullptr) statusText_->setText(presence_phrase(presence));
    if (statusDot_ != nullptr)
        statusDot_->setStyleSheet(
            QStringLiteral("color:%1;").arg(presence_accent(presence).name()));
}

void MainWindow::flashRemembering() {
    remembering_ = true;
    applyPresence(Presence::Remembering);
    rememberTimer_->start(1400);
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

    // A fresh question overrides the previous answer's "filing away" beat.
    remembering_ = false;
    rememberTimer_->stop();

    pending_ = utterance;
    home_->setExchange(utterance, QString());
    home_->setThinking();
    // The workspace reads what you asked and rearranges toward it.
    home_->workspace()->setContext(home_->workspace()->inferred(utterance));

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

    // A few actions are about a place in the app, not only an answer. Asking
    // for her notes should put them on screen, not just say something about
    // them -- otherwise "open my notes" is the one request that leaves you
    // exactly where you started.
    if (action == QStringLiteral("open_notes") || action == QStringLiteral("take_note") ||
        action == QStringLiteral("read_notes") || action == QStringLiteral("ask_notes")) {
        navigate(PageNotes);
    }

    // The exchange becomes a memory the moment it completes.
    timeline_->remember(pending_, reply);

    say(reply);
}

void MainWindow::say(const QString& text) {
    // Answers are always on screen; speaking them is the part you can switch
    // off. Somewhere quiet, or next to someone, a spoken reply is the wrong
    // default -- and until now there was no way to have her answer silently.
    if (!speakReplies_) return;
    if (!speaker_ || !listener_) return;
    listener_->set_speaking(true);
    speaker_->speak(text.toStdString(), [this](bool completed) {
        QMetaObject::invokeMethod(this, [this, completed] {
            if (listener_) listener_->set_speaking(false);
            // Only file it away as a finished answer -- not when she was cut off
            // mid-sentence by a barge-in or Stop.
            if (completed) flashRemembering();
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
