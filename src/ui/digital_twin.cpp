#include "ui/digital_twin.hpp"

#include "ui/glass_card.hpp"

#include <QLabel>
#include <QVBoxLayout>

#include <array>

namespace mimi::ui {
namespace {

struct Trait {
    const char* tag;
    const char* title;
    const char* body;
};

constexpr std::array<Trait, 5> kTraits{{
    {"WRITING STYLE", "Concise, warm, low on jargon", "Learned from 240 of your messages."},
    {"RHYTHM", "You ship around 4 PM", "A consistent pattern across six weeks."},
    {"PROJECTS", "Mimi · Marketing proposal", "Where most of your attention goes lately."},
    {"CODING", "C++ / Qt, small frequent commits", "From your recent working style."},
    {"PEOPLE", "Acme, John, the core team", "Who you collaborate with most."},
}};

}  // namespace

DigitalTwin::DigitalTwin(QWidget* parent) : QWidget(parent) {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(64, 40, 64, 40);
    column->setSpacing(10);

    auto* head = new QLabel(QStringLiteral("YOUR DIGITAL TWIN"));
    head->setObjectName(QStringLiteral("sectionHead"));
    column->addWidget(head, 0, Qt::AlignHCenter);

    auto* lead = new QLabel(QStringLiteral("What Mimi has learned about how you work."));
    lead->setObjectName(QStringLiteral("sectionLead"));
    column->addWidget(lead, 0, Qt::AlignHCenter);

    column->addSpacing(16);

    for (const Trait& t : kTraits) {
        auto* card = new GlassCard(QString::fromUtf8(t.tag), QString::fromUtf8(t.title),
                                   QString::fromUtf8(t.body));
        card->setMaximumWidth(680);
        card->setFixedHeight(92);
        column->addWidget(card, 0, Qt::AlignHCenter);
    }

    column->addStretch(1);
}

}  // namespace mimi::ui
