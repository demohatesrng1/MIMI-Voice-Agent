#include "ui/settings_view.hpp"

#include "brain/accessibility.hpp"
#include "brain/account.hpp"
#include "brain/notes.hpp"
#include "core/paths.hpp"
#include "ui/theme.hpp"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

namespace mimi::ui {
namespace {

using brain::ax::Access;

struct State {
    QString text;
    QColor colour;
};

State state_for(Access access) {
    switch (access) {
        case Access::Granted:  return {QStringLiteral("Granted"), theme::kAccentSoft};
        case Access::Denied:   return {QStringLiteral("Denied"), theme::kError};
        case Access::NotAsked: return {QStringLiteral("Not asked yet"), theme::kWarn};
        case Access::Unknown:  break;
    }
    return {QStringLiteral("Unknown"), theme::kDim};
}

State state_for(bool granted) {
    return granted ? State{QStringLiteral("Granted"), theme::kAccentSoft}
                   : State{QStringLiteral("Not granted"), theme::kError};
}

void reveal(const std::string& path) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(path)));
}

}  // namespace

SettingsView::SettingsView(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    outer->addWidget(scroll);

    auto* page = new QWidget;
    page->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(40, 32, 40, 40);
    column->setSpacing(0);

    auto* title = new QLabel(QStringLiteral("Settings"));
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 8);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    title->setStyleSheet(QStringLiteral("color: %1;").arg(theme::kInk.name()));
    column->addWidget(title);

    auto* blurb = new QLabel(
        QStringLiteral("Everything Mimi hears, says and remembers stays on this Mac. "
                       "macOS grants each permission separately."));
    blurb->setWordWrap(true);
    blurb->setStyleSheet(QStringLiteral("color: %1;").arg(theme::kDim.name()));
    column->addWidget(blurb);
    column->addSpacing(28);

    // Who is signed in, and the way out. Signing in was previously a one-way
    // door: no way to see which account was in use, and no way to leave it.
    addHeading(column, QStringLiteral("ACCOUNT"));
    account_ = addRow(column, QStringLiteral("Signed in"),
                      QStringLiteral("Stored on this Mac. Signing out does not delete "
                                     "your notes or memory."),
                      QStringLiteral("Sign out"), [this] {
                          if (brain::Accounts().forget()) refresh();
                      });
    column->addSpacing(28);

    addHeading(column, QStringLiteral("PERMISSIONS"));
    accessibility_ = addRow(
        column, QStringLiteral("Accessibility"),
        QStringLiteral("Lets her press buttons and read the window you're working in."),
        QStringLiteral("Open Settings"), [] { brain::ax::open_permission_settings(); });
    microphone_ = addRow(
        column, QStringLiteral("Microphone"),
        QStringLiteral("Needed for the wake word and everything you say to her."),
        QStringLiteral("Open Settings"), [] { brain::ax::open_microphone_settings(); });
    contacts_ = addRow(
        column, QStringLiteral("Contacts"),
        QStringLiteral("Only read when you ask her to call someone by name."),
        QStringLiteral("Open Settings"), [] { brain::ax::open_contacts_settings(); });
    screen_ = addRow(
        column, QStringLiteral("Screen Recording"),
        QStringLiteral("Required by macOS for screenshots."),
        QStringLiteral("Open Settings"),
        [] { brain::ax::open_screen_recording_settings(); });

    column->addSpacing(28);
    addHeading(column, QStringLiteral("VOICE AND THINKING"));
    voice_ = addRow(column, QStringLiteral("Speech"),
                    QStringLiteral("Which voice she answers in."), {}, {});
    brain_ = addRow(column, QStringLiteral("Language model"),
                    QStringLiteral("Runs locally through Ollama."), {}, {});

    column->addSpacing(28);
    addHeading(column, QStringLiteral("HER DATA"));
    notes_ = addRow(column, QStringLiteral("Notes"),
                    QStringLiteral("Markdown files you can read without Mimi."),
                    QStringLiteral("Show in Finder"),
                    [] { reveal(brain::Notes().directory()); });
    dataDir_ = addRow(column, QStringLiteral("Everything she writes"),
                      QStringLiteral("Journal, notes, digests and her profile."),
                      QStringLiteral("Show in Finder"),
                      [] { reveal(paths::data_dir().string()); });
    modelsDir_ = addRow(column, QStringLiteral("Models"),
                        QStringLiteral("Whisper, the voice, and the wake word."),
                        QStringLiteral("Show in Finder"),
                        [] { reveal(paths::models_dir().string()); });

    column->addStretch(1);
    scroll->setWidget(page);

    refresh();
}

void SettingsView::addHeading(QVBoxLayout* into, const QString& text) {
    auto* heading = new QLabel(text);
    QFont font = heading->font();
    font.setPointSize(font.pointSize() - 2);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
    heading->setFont(font);
    heading->setStyleSheet(QStringLiteral("color: %1;").arg(theme::kFaint.name()));
    into->addWidget(heading);
    into->addSpacing(10);
}

SettingsView::Row SettingsView::addRow(QVBoxLayout* into, const QString& name,
                                       const QString& hint, const QString& action,
                                       std::function<void()> on_action) {
    auto* card = new QWidget;
    card->setStyleSheet(QStringLiteral("background: %1; border-radius: 12px;")
                            .arg(theme::kLayer1.name()));
    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(18, 14, 18, 14);
    row->setSpacing(16);

    auto* left = new QVBoxLayout;
    left->setSpacing(3);
    auto* label = new QLabel(name);
    QFont labelFont = label->font();
    labelFont.setWeight(QFont::Medium);
    label->setFont(labelFont);
    label->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                             .arg(theme::kInk.name()));
    left->addWidget(label);

    auto* detail = new QLabel(hint);
    detail->setWordWrap(true);
    QFont detailFont = detail->font();
    detailFont.setPointSize(detailFont.pointSize() - 1);
    detail->setFont(detailFont);
    // The line explaining what a permission is for is the reason the row
    // exists; it should not be quieter than the label above it by much.
    detail->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                              .arg(theme::kDim.name()));
    left->addWidget(detail);
    row->addLayout(left, 1);

    auto* value = new QLabel;
    QFont valueFont = value->font();
    valueFont.setWeight(QFont::DemiBold);
    value->setFont(valueFont);
    value->setStyleSheet(QStringLiteral("background: transparent;"));
    row->addWidget(value, 0, Qt::AlignVCenter);

    if (!action.isEmpty() && on_action) {
        auto* button = new QPushButton(action);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(
            QStringLiteral("QPushButton { background: %1; color: %2; border: none;"
                           "  border-radius: 8px; padding: 7px 14px; }"
                           "QPushButton:hover { background: %3; }")
                .arg(theme::kLayer2.name(), theme::kInk.name(),
                     theme::kAccentDeep.name()));
        connect(button, &QPushButton::clicked, this,
                [on_action] { on_action(); });
        row->addWidget(button, 0, Qt::AlignVCenter);
    }

    into->addWidget(card);
    into->addSpacing(8);
    return Row{value, detail};
}

void SettingsView::refresh() {
    const auto apply = [](const Row& row, const State& state) {
        if (row.value == nullptr) return;
        row.value->setText(state.text);
        row.value->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                     .arg(state.colour.name()));
    };
    const auto plain = [](const Row& row, const QString& text) {
        if (row.value == nullptr) return;
        row.value->setText(text);
        row.value->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                     .arg(theme::kDim.name()));
    };

    const brain::Account account = brain::Accounts().load();
    plain(account_, account.valid()
                        ? QString::fromStdString(account.preferred.empty()
                                                     ? account.username
                                                     : account.preferred)
                        : QStringLiteral("Not signed in"));

    apply(accessibility_, state_for(brain::ax::has_permission()));
    apply(microphone_, state_for(brain::ax::microphone_access()));
    apply(contacts_, state_for(brain::ax::contacts_access()));
    apply(screen_, state_for(brain::ax::screen_recording_access()));

    // Whether VOICEVOX was fetched decides which voice she actually speaks in;
    // the build falls back to the system voice when it is missing.
#ifdef MIMI_HAS_VOICEVOX
    plain(voice_, QStringLiteral("VOICEVOX (neural)"));
#else
    plain(voice_, QStringLiteral("Kyoko (system)"));
#endif
    plain(brain_, QStringLiteral("gemma3n:e4b"));

    const int count = static_cast<int>(brain::Notes().all().size());
    plain(notes_, count == 1 ? QStringLiteral("1 note")
                             : QStringLiteral("%1 notes").arg(count));
    plain(dataDir_, QStringLiteral("On this Mac"));
    plain(modelsDir_, QStringLiteral("On this Mac"));
}

void SettingsView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // macOS never reports a permission change back, so the only reliable moment
    // to re-read is when the page becomes visible again.
    refresh();
}

}  // namespace mimi::ui
