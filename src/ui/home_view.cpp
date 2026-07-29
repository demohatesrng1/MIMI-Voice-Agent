#include "ui/home_view.hpp"

#include "ui/icons.hpp"
#include "ui/theme.hpp"
#include "ui/voice_orb.hpp"
#include "voice/listener.hpp"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <array>

namespace mimi::ui {
namespace {

// Label, and the utterance it stands for. Chosen to span the range: read the
// machine, change the machine, reach the web, set a timer. Someone who has
// never used her should be able to work out what she is for from this grid.
struct Tile {
    const char* label;
    icons::Glyph glyph;
    // Sent to the router in Japanese, because that is the language she runs in.
    // Only the label the user reads is English.
    const char* utterance;
};

const std::array<Tile, 6> kTiles{{
    {"Time",       icons::Glyph::Clock,      "今何時ですか"},
    {"Battery",    icons::Glyph::Battery,    "バッテリーはどのくらい"},
    {"System",     icons::Glyph::Display,    "システムの空き容量は"},
    {"Screenshot", icons::Glyph::Camera,     "スクリーンショットを撮って"},
    {"Volume down",icons::Glyph::VolumeDown, "音量を下げて"},
    {"Lock screen",icons::Glyph::Lock,       "画面をロックして"},
}};

}  // namespace

HomeView::HomeView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("home"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(48, 30, 48, 26);
    layout->setSpacing(0);

    layout->addStretch(2);

    orb_ = new VoiceOrb;
    orb_->setFixedSize(168, 168);
    layout->addWidget(orb_, 0, Qt::AlignHCenter);

    layout->addSpacing(22);

    stateLabel_ = new QLabel(QStringLiteral("STARTING"));
    stateLabel_->setObjectName(QStringLiteral("heroState"));
    stateLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(stateLabel_);

    layout->addSpacing(26);

    // The exchange in progress. What you said sits above, quiet and small;
    // her answer below, large. The eye should land on the answer.
    saidLabel_ = new QLabel;
    saidLabel_->setObjectName(QStringLiteral("heroSaid"));
    saidLabel_->setAlignment(Qt::AlignCenter);
    saidLabel_->setWordWrap(true);
    saidLabel_->setVisible(false);
    layout->addWidget(saidLabel_);

    layout->addSpacing(10);

    replyLabel_ = new QLabel(QStringLiteral("Say \u201chey mimi\u201d, or type below"));
    replyLabel_->setObjectName(QStringLiteral("heroReply"));
    replyLabel_->setAlignment(Qt::AlignCenter);
    replyLabel_->setWordWrap(true);
    replyLabel_->setMinimumHeight(72);
    layout->addWidget(replyLabel_);

    layout->addStretch(3);
    layout->addWidget(buildTiles());
}

QWidget* HomeView::buildTiles() {
    auto* holder = new QWidget;
    auto* grid = new QGridLayout(holder);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    int column = 0;
    int row = 0;
    for (const auto& tile : kTiles) {
        auto* button = new QPushButton(QString::fromUtf8(tile.label));
        button->setObjectName(QStringLiteral("tile"));
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(46);
        button->setIcon(icons::icon(tile.glyph, QColor(0xa4, 0xa4, 0xba), 17));
        button->setIconSize(QSize(17, 17));
        const QString utterance = QString::fromUtf8(tile.utterance);
        connect(button, &QPushButton::clicked, this,
                [this, utterance] { Q_EMIT commandRequested(utterance); });
        grid->addWidget(button, row, column);
        if (++column == 3) {
            column = 0;
            ++row;
        }
    }
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
    if (!replied.isEmpty()) replyLabel_->setText(replied);
}

void HomeView::setThinking() { replyLabel_->setText(QStringLiteral("…")); }

}  // namespace mimi::ui
