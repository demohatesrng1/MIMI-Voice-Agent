#include "ui/skills_view.hpp"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>

namespace mimi::ui {
namespace {

struct Skill {
    const char* title;
    const char* phrase;   // say this, verbatim
    const char* detail;
};

struct Group {
    const char* name;
    std::array<Skill, 4> skills;
};

// Grouped by what the user is trying to do, not by which module implements it.
// Nobody thinks "I need the AppleScript layer"; they think "I want music".
const std::array<Group, 3> kGroups{{
    {"CONTROL YOUR MAC",
     {{
         {"Launch an app", "スポティファイを起動して", "Just say the name"},
         {"Open a website", "ユーチューブを開いて", "Opens in your browser"},
         {"Volume", "音量を下げて", "Up, down, or mute"},
         {"Lock the screen", "画面をロックして", "Locks immediately"},
     }}},
    {"CHECK STATUS",
     {{
         {"Time", "今何時ですか", "Date and weekday too"},
         {"Battery", "バッテリーはどのくらい", "Level and charge state"},
         {"System", "システムの空き容量は", "Memory and disk"},
         {"Screenshot", "スクリーンショットを撮って", "Saved to the Desktop"},
     }}},
    {"EVERYTHING ELSE",
     {{
         {"Reminder", "5分後に休憩と教えて", "She tells you when it is time"},
         {"Web search", "猫の動画を検索して", "Open a result by number"},
         {"Clipboard", "クリップボードを要約して", "Summarises what you copied"},
         {"Ask anything", "日本の首都はどこ", "Ordinary conversation works too"},
     }}},
}};

}  // namespace

SkillsView::SkillsView(QWidget* parent) : QScrollArea(parent) {
    setObjectName(QStringLiteral("skills"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("skillsContent"));
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(30, 26, 30, 26);
    layout->setSpacing(0);

    auto* heading = new QLabel(QStringLiteral("SKILLS"));
    heading->setObjectName(QStringLiteral("sectionHead"));
    layout->addWidget(heading);
    layout->addSpacing(4);

    auto* lead = new QLabel(QStringLiteral("Say any of these out loud, or click a card to run it."));
    lead->setObjectName(QStringLiteral("sectionLead"));
    layout->addWidget(lead);
    layout->addSpacing(22);

    for (const auto& group : kGroups) {
        auto* name = new QLabel(QString::fromUtf8(group.name));
        name->setObjectName(QStringLiteral("groupHead"));
        layout->addWidget(name);
        layout->addSpacing(10);

        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(10);

        int column = 0;
        int row = 0;
        for (const auto& skill : group.skills) {
            auto* card = new QPushButton;
            card->setObjectName(QStringLiteral("skillCard"));
            card->setCursor(Qt::PointingHandCursor);
            card->setMinimumHeight(84);

            auto* inner = new QVBoxLayout(card);
            inner->setContentsMargins(15, 12, 15, 12);
            inner->setSpacing(3);

            auto* title = new QLabel(QString::fromUtf8(skill.title));
            title->setObjectName(QStringLiteral("skillTitle"));
            inner->addWidget(title);

            auto* phrase = new QLabel(QStringLiteral("「%1」")
                                          .arg(QString::fromUtf8(skill.phrase)));
            phrase->setObjectName(QStringLiteral("skillPhrase"));
            phrase->setWordWrap(true);
            inner->addWidget(phrase);

            auto* detail = new QLabel(QString::fromUtf8(skill.detail));
            detail->setObjectName(QStringLiteral("skillDetail"));
            inner->addWidget(detail);

            // The labels would otherwise swallow the click before the button.
            for (QLabel* child : card->findChildren<QLabel*>()) {
                child->setAttribute(Qt::WA_TransparentForMouseEvents);
            }

            const QString utterance = QString::fromUtf8(skill.phrase);
            connect(card, &QPushButton::clicked, this,
                    [this, utterance] { Q_EMIT commandRequested(utterance); });

            grid->addWidget(card, row, column);
            if (++column == 2) {
                column = 0;
                ++row;
            }
        }
        layout->addLayout(grid);
        layout->addSpacing(26);
    }

    layout->addStretch(1);
    setWidget(content);
}

}  // namespace mimi::ui
