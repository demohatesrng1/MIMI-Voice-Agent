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
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
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
    resize(1120, 760);
    setMinimumSize(940, 660);

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

    // The context strip lives at the foot, as a status bar. Directly under the
    // title bar it was a second band of chrome saying very little -- two rows
    // of furniture before any content, which is the thing an editor never does.
    // At the bottom it reads as status, which is what it is.
    ribbon_ = new ContextRibbon;
    column->addWidget(ribbon_);
    refreshContext();  // after the ribbon exists, or it fills nothing

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

    // Floating overlays, parented to the ambient root so they hover over the
    // pages. Positioned by layoutOverlays(), not the column layout.
    palette_ = new CommandPalette(ambient_);
    connect(palette_, &CommandPalette::commandChosen, this, &MainWindow::ask);
    connect(palette_, &CommandPalette::navigateChosen, this, &MainWindow::navigate);

    search_ = new NeuralSearch(ambient_);
    connect(search_, &NeuralSearch::navigateChosen, this, &MainWindow::navigate);

    auto* paletteKey = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    connect(paletteKey, &QShortcut::activated, this, [this] { palette_->open(); });
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

    // Navigation. A rule separates it from the wordmark, so the row reads as
    // "name, then places" rather than four icons trailing a label.
    layout->addSpacing(14);
    auto* rule = new QWidget;
    rule->setObjectName(QStringLiteral("chromeRule"));
    rule->setFixedSize(1, 18);
    layout->addWidget(rule);
    layout->addSpacing(10);
    navHome_ = new GhostButton(icons::Glyph::Home);
    navHome_->setCheckable(true);
    navHome_->setToolTip(QStringLiteral("Home"));
    connect(navHome_, &GhostButton::clicked, this, [this] { navigate(PageHome); });
    layout->addWidget(navHome_);

    navTimeline_ = new GhostButton(icons::Glyph::Timeline);
    navTimeline_->setCheckable(true);
    navTimeline_->setToolTip(QStringLiteral("Memory — everything she has done"));
    connect(navTimeline_, &GhostButton::clicked, this, [this] { navigate(PageTimeline); });
    layout->addWidget(navTimeline_);

    navNotes_ = new GhostButton(icons::Glyph::Files);
    navNotes_->setCheckable(true);
    navNotes_->setToolTip(QStringLiteral("Notes"));
    connect(navNotes_, &GhostButton::clicked, this, [this] { navigate(PageNotes); });
    layout->addWidget(navNotes_);

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

    // Interrupting her, as a button.
    //
    // Talking over her works, but it needs a raised voice and a working
    // microphone, and neither is guaranteed while she is mid-sentence with the
    // speakers up. A control that is simply there whenever she is talking is
    // the one way to stop her that cannot fail. Hidden the rest of the time --
    // it has nothing to do until she speaks.
    stopBtn_ = new QPushButton(QStringLiteral("Stop"));
    stopBtn_->setObjectName(QStringLiteral("stopBtn"));
    stopBtn_->setCursor(Qt::PointingHandCursor);
    stopBtn_->setFixedHeight(26);
    stopBtn_->setToolTip(QStringLiteral("Stop talking"));
    stopBtn_->setVisible(false);
    connect(stopBtn_, &QPushButton::clicked, this, [this] {
        if (speaker_) speaker_->stop();
        if (listener_) listener_->set_speaking(false);
#ifdef MIMI_HAS_AVATAR
        if (auto* avatar = home_->avatar()) avatar->clearVisemes();
#endif
    });
    layout->addWidget(stopBtn_);

    voicePill_ = new QPushButton(QStringLiteral("Voice"));
    voicePill_->setObjectName(QStringLiteral("mutePill"));
    voicePill_->setCheckable(true);
    voicePill_->setCursor(Qt::PointingHandCursor);
    voicePill_->setFixedHeight(26);
    voicePill_->setToolTip(QStringLiteral("Speak answers aloud, or reply in text only"));
    connect(voicePill_, &QPushButton::toggled, this, [this](bool silent) {
        speakReplies_ = !silent;
        voicePill_->setText(silent ? QStringLiteral("Text") : QStringLiteral("Voice"));
        if (silent && speaker_) speaker_->stop();
    });
    layout->addWidget(voicePill_);

    mutePill_ = new QPushButton(QStringLiteral("Mute"));
    mutePill_->setObjectName(QStringLiteral("mutePill"));
    mutePill_->setCheckable(true);
    mutePill_->setCursor(Qt::PointingHandCursor);
    mutePill_->setFixedHeight(30);
    mutePill_->setToolTip(QStringLiteral("Stop listening"));
    connect(mutePill_, &QPushButton::toggled, this, [this](bool muted) {
        setMuted(muted);
        mutePill_->setText(muted ? QStringLiteral("Unmute") : QStringLiteral("Mute"));
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
