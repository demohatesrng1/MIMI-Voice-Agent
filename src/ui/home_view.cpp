#include "ui/home_view.hpp"

#include "ui/controls.hpp"
#include "ui/voice_orb.hpp"
#include "voice/listener.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <array>

namespace mimi::ui {
namespace {

// Label, and the utterance it stands for. Four, not a wall: enough to teach
// the range -- read the machine, change it, capture it, secure it -- without
// turning the hero surface into a control panel.
struct Suggestion {
    const char* label;
    // Sent to the router in Japanese, because that is the language she runs in.
    // Only the label the user reads is English.
    const char* utterance;
};

const std::array<Suggestion, 4> kSuggestions{{
    {"What time is it?", "今何時ですか"},
    {"Battery status", "バッテリーはどのくらい"},
    {"Take a screenshot", "スクリーンショットを撮って"},
    {"Lock the screen", "画面をロックして"},
}};

}  // namespace

HomeView::HomeView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("home"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(64, 24, 64, 20);
    layout->setSpacing(0);

    layout->addStretch(3);

    orb_ = new VoiceOrb;
    orb_->setFixedSize(200, 200);
    layout->addWidget(orb_, 0, Qt::AlignHCenter);

    layout->addSpacing(20);

    stateLabel_ = new QLabel(QStringLiteral("STARTING"));
    stateLabel_->setObjectName(QStringLiteral("heroState"));
    stateLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(stateLabel_);

    layout->addSpacing(28);

    // The exchange in progress. What you said sits above, quiet and small;
    // her answer below, large. The eye should land on the answer.
    saidLabel_ = new QLabel;
    saidLabel_->setObjectName(QStringLiteral("heroSaid"));
    saidLabel_->setAlignment(Qt::AlignCenter);
    saidLabel_->setWordWrap(true);
    saidLabel_->setVisible(false);
    layout->addWidget(saidLabel_);

    layout->addSpacing(10);

    replyLabel_ = new QLabel(QStringLiteral("Ask anything — or just start talking."));
    replyLabel_->setObjectName(QStringLiteral("heroReply"));
    replyLabel_->setAlignment(Qt::AlignCenter);
    replyLabel_->setWordWrap(true);
    replyLabel_->setMinimumHeight(76);
    layout->addWidget(replyLabel_);

    layout->addStretch(4);
    layout->addWidget(buildChips());

    // Thinking is animated, not static: a breathing ellipsis on a slow beat,
    // so waiting reads as her working rather than the app hanging.
    thinkingTick_ = new QTimer(this);
    thinkingTick_->setInterval(400);
    connect(thinkingTick_, &QTimer::timeout, this, [this] {
        thinkingBeat_ = (thinkingBeat_ + 1) % 3;
        replyLabel_->setText(QStringLiteral("·  ·  ·").left(3 * thinkingBeat_ + 1));
    });
}

QWidget* HomeView::buildChips() {
    auto* holder = new QWidget;
    auto* row = new QHBoxLayout(holder);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    row->addStretch(1);

    for (const auto& suggestion : kSuggestions) {
        auto* chip = new Chip(QString::fromUtf8(suggestion.label));
        const QString utterance = QString::fromUtf8(suggestion.utterance);
        connect(chip, &Chip::clicked, this,
                [this, utterance] { Q_EMIT commandRequested(utterance); });
        row->addWidget(chip);
    }

    row->addStretch(1);
    return holder;
}

void HomeView::setState(int state) {
    orb_->setState(state);
    const auto value = static_cast<voice::State>(state);
    switch (value) {
        case voice::State::Idle:      stateLabel_->setText(QStringLiteral("READY")); break;
        case voice::State::Listening: stateLabel_->setText(QStringLiteral("LISTENING")); break;
        case voice::State::Thinking:  stateLabel_->setText(QStringLiteral("THINKING")); break;
        case voice::State::Speaking:  stateLabel_->setText(QStringLiteral("SPEAKING")); break;
        case voice::State::Paused:    stateLabel_->setText(QStringLiteral("MUTED")); break;
    }
    stateLabel_->setProperty("mode", static_cast<int>(value));
    // Re-polish so the stylesheet picks up the changed property.
    stateLabel_->style()->unpolish(stateLabel_);
    stateLabel_->style()->polish(stateLabel_);
}

void HomeView::setLevel(float rms) { orb_->setLevel(rms); }

void HomeView::setExchange(const QString& said, const QString& replied) {
    saidLabel_->setText(said.isEmpty() ? QString() : QStringLiteral("「%1」").arg(said));
    saidLabel_->setVisible(!said.isEmpty());
    if (!replied.isEmpty()) {
        thinkingTick_->stop();
        replyLabel_->setText(replied);
    }
}

void HomeView::setThinking() {
    thinkingBeat_ = 0;
    replyLabel_->setText(QStringLiteral("·"));
    thinkingTick_->start();
}

}  // namespace mimi::ui
