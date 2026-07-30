#include "ui/mission_control.hpp"

#include "ui/controls.hpp"
#include "ui/glass_card.hpp"

#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <array>

namespace mimi::ui {
namespace {

struct Mission {
    const char* title;
    const char* subtitle;
};

constexpr std::array<Mission, 3> kMissions{{
    {"Prepare tomorrow's client meeting", "Acme Corp · 9:00 AM · 5 sources, 3 apps"},
    {"Weekly review", "This week's work, summarized and filed"},
    {"Draft the Q3 proposal", "From the kickoff notes and last quarter's numbers"},
}};

struct Step {
    const char* tag;
    const char* title;
    const char* body;
};

// What she assembles. Shared across missions for this first pass.
constexpr std::array<Step, 7> kSteps{{
    {"EMAIL", "Latest threads", "3 from Acme, newest 2h ago — pricing still open."},
    {"FILES", "Relevant documents", "Proposal v3, Kickoff notes, Q2 numbers."},
    {"MEMORY", "Last meeting", "Agreed scope; pricing and timeline left unresolved."},
    {"TASKS", "Open action items", "2 unresolved: confirm pricing, lock the timeline."},
    {"AGENDA", "Suggested agenda", "1. Recap   2. Pricing   3. Timeline   4. Next steps."},
    {"POINTS", "Talking points", "Lead with the delivery-date win; anchor price to scope."},
    {"DRAFT", "Presentation draft", "6 slides drafted from the proposal and notes."},
}};

}  // namespace

MissionControl::MissionControl(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("missions"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    stack_ = new QStackedWidget;
    stack_->addWidget(buildLauncher());  // 0
    stack_->addWidget(buildBoard());     // 1
    outer->addWidget(stack_);

    reveal_ = new QTimer(this);
    reveal_->setInterval(360);
    connect(reveal_, &QTimer::timeout, this, &MissionControl::revealNext);
}

QWidget* MissionControl::buildLauncher() {
    auto* page = new QWidget;
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(64, 40, 64, 40);
    column->setSpacing(10);

    auto* head = new QLabel(QStringLiteral("MISSIONS"));
    head->setObjectName(QStringLiteral("sectionHead"));
    column->addWidget(head, 0, Qt::AlignHCenter);

    auto* lead = new QLabel(
        QStringLiteral("Open a mission and Mimi assembles the whole workspace for it."));
    lead->setObjectName(QStringLiteral("sectionLead"));
    column->addWidget(lead, 0, Qt::AlignHCenter);

    column->addSpacing(18);

    for (int i = 0; i < static_cast<int>(kMissions.size()); ++i) {
        auto* card = new GlassCard(QStringLiteral("MISSION"),
                                   QString::fromUtf8(kMissions[i].title),
                                   QString::fromUtf8(kMissions[i].subtitle));
        card->setMaximumWidth(680);
        card->setFixedHeight(104);
        connect(card, &GlassCard::clicked, this, [this, i] { openMission(i); });
        column->addWidget(card, 0, Qt::AlignHCenter);
    }

    column->addStretch(1);
    return page;
}

QWidget* MissionControl::buildBoard() {
    auto* page = new QWidget;
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(64, 28, 64, 24);
    column->setSpacing(6);

    auto* back = new Chip(QStringLiteral("‹  Missions"));
    connect(back, &Chip::clicked, this, [this] {
        reveal_->stop();
        stack_->setCurrentIndex(0);
    });
    column->addWidget(back, 0, Qt::AlignLeft);

    column->addSpacing(6);
    boardTitle_ = new QLabel;
    boardTitle_->setObjectName(QStringLiteral("heroReply"));
    boardTitle_->setWordWrap(true);
    column->addWidget(boardTitle_);

    boardStatus_ = new QLabel;
    boardStatus_->setObjectName(QStringLiteral("sectionHead"));
    column->addWidget(boardStatus_);

    column->addSpacing(12);

    // The assembled steps scroll; they are added one at a time as she works.
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("missions"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setAttribute(Qt::WA_TranslucentBackground);
    scroll->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* holder = new QWidget;
    steps_ = new QVBoxLayout(holder);
    steps_->setContentsMargins(0, 0, 0, 0);
    steps_->setSpacing(10);
    steps_->addStretch(1);
    scroll->setWidget(holder);
    column->addWidget(scroll, 1);

    return page;
}

void MissionControl::openMission(int index) {
    boardTitle_->setText(QString::fromUtf8(kMissions[index].title));
    boardStatus_->setText(QStringLiteral("ASSEMBLING…"));

    // Clear any previous run, keeping the trailing stretch.
    while (steps_->count() > 1) {
        QLayoutItem* item = steps_->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    revealed_ = 0;
    stack_->setCurrentIndex(1);
    reveal_->start();
}

void MissionControl::revealNext() {
    if (revealed_ >= static_cast<int>(kSteps.size())) {
        reveal_->stop();
        boardStatus_->setText(QStringLiteral("MISSION READY"));
        return;
    }
    const Step& s = kSteps[revealed_++];
    auto* card = new GlassCard(QString::fromUtf8(s.tag), QString::fromUtf8(s.title),
                               QString::fromUtf8(s.body));
    card->setFixedHeight(96);
    // Insert before the trailing stretch so cards stack top-down.
    steps_->insertWidget(steps_->count() - 1, card);
}

}  // namespace mimi::ui
