#include "ui/notes_view.hpp"

#include "ui/icons.hpp"
#include "brain/tools.hpp"
#include "ui/theme.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace mimi::ui {
namespace {

QString stylesheet() {
    const auto hex = [](const QColor& colour) { return colour.name(QColor::HexRgb); };
    // Selection is a raised row, not a coloured one. A saturated fill behind
    // whichever note happens to be open is the loudest thing on the page, and
    // it is only telling you where the cursor is.
    return QStringLiteral(
               "QListWidget { background: %1; border: 1px solid %5;"
               "  border-radius: 10px; padding: 5px; color: %2; }"
               "QListWidget::item { padding: 11px 12px; border-radius: 6px; }"
               "QListWidget::item:hover { background: %6; }"
               "QListWidget::item:selected { background: %3; color: %4; }"
               "QTextEdit { background: %1; border: 1px solid %5; border-radius: 10px;"
               "  padding: 16px; color: %2; selection-background-color: %7; }"
               "QPushButton { background: %3; border: 1px solid %5; border-radius: 7px;"
               "  color: %2; padding: 8px 14px; font-size: 16px; }"
               "QPushButton:hover { background: %6; border: 1px solid %8; }")
        .arg(hex(theme::kLayer1), hex(theme::kInk), hex(theme::kLayer2),
             hex(theme::kInk), hex(theme::kLine), hex(theme::kLayer2),
             hex(theme::kAccentDeep), hex(theme::kAccent));
}

}  // namespace

NotesView::NotesView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    // --- the index -----------------------------------------------------------
    auto* left = new QVBoxLayout;
    left->setSpacing(10);

    auto* heading = new QLabel(QStringLiteral("Notes"));
    // Through the stylesheet: the application sheet sets a size on QWidget, and
    // that beats setFont().
    heading->setStyleSheet(QStringLiteral("color: %1; font-size: 24px; font-weight: 600;")
                               .arg(theme::kInk.name()));
    left->addWidget(heading);

    // Filtering the index. With twenty notes in a sidebar, hunting by eye is
    // the obvious thing the page was missing.
    filter_ = new QLineEdit;
    filter_->setPlaceholderText(QStringLiteral("Filter notes"));
    filter_->setFixedWidth(320);
    filter_->setFixedHeight(34);
    filter_->setClearButtonEnabled(true);
    connect(filter_, &QLineEdit::textChanged, this, [this] { refresh(); });
    left->addWidget(filter_);

    list_ = new QListWidget;
    list_->setFixedWidth(320);
    // Titles are one line and elided by the delegate; a horizontal bar under
    // the list is a scrollbar for content that is never meant to scroll.
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setTextElideMode(Qt::ElideRight);
    list_->setUniformItemSizes(true);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int) { showSelected(); });
    left->addWidget(list_, 1);

    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("New note"));
    connect(add, &QPushButton::clicked, this, &NotesView::addNote);
    buttons->addWidget(add);
    auto* remove = new QPushButton(QStringLiteral("Delete"));
    connect(remove, &QPushButton::clicked, this, &NotesView::deleteSelected);
    buttons->addWidget(remove);
    left->addLayout(buttons);

    // Reminders waiting to go off. They live here because this is the page for
    // things she is holding on your behalf -- and until now a reminder you set
    // was invisible, with no way to confirm it or call it off.
    reminderPanel_ = new QWidget;
    reminderPanel_->setFixedWidth(320);
    reminderRows_ = new QVBoxLayout(reminderPanel_);
    reminderRows_->setContentsMargins(0, 14, 0, 0);
    reminderRows_->setSpacing(6);
    left->addWidget(reminderPanel_);

    layout->addLayout(left);

    // --- the note ------------------------------------------------------------
    auto* right = new QVBoxLayout;
    right->setSpacing(10);

    stamp_ = new QLabel;
    stamp_->setStyleSheet(QStringLiteral("color: %1;").arg(theme::kDim.name()));
    right->addWidget(stamp_);

    body_ = new QTextEdit;
    body_->setPlaceholderText(
        QStringLiteral("Say \"…とメモして\" and it appears here."));
    right->addWidget(body_, 1);

    empty_ = new QLabel(
        QStringLiteral("No notes yet.\n\nSay \"牛乳を買うとメモして\" — or "
                       "\"take a note that…\" — and Mimi writes it here."));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setStyleSheet(QStringLiteral("color: %1;").arg(theme::kFaint.name()));
    right->addWidget(empty_, 1);
    layout->addLayout(right, 1);

    setStyleSheet(stylesheet());

    // Typing saves shortly after you stop, rather than on every keystroke.
    auto* debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(600);
    connect(debounce, &QTimer::timeout, this, &NotesView::saveCurrent);
    connect(body_, &QTextEdit::textChanged, this, [this, debounce] {
        if (!loading_) debounce->start();
    });

    refresh();
}

void NotesView::refresh() {
    const std::string keep = current_;
    loading_ = true;
    list_->clear();

    const QString needle = filter_ == nullptr ? QString() : filter_->text().trimmed();
    const auto notes =
        needle.isEmpty() ? notes_.all() : notes_.search(needle.toStdString(), 0);
    for (const auto& note : notes) {
        auto* item = new QListWidgetItem(QString::fromStdString(note.title));
        item->setData(Qt::UserRole, QString::fromStdString(note.id));
        list_->addItem(item);
    }
    loading_ = false;

    const bool any = !notes.empty();
    body_->setVisible(any);
    stamp_->setVisible(any);
    empty_->setVisible(!any);

    if (any) {
        // Keep the note that was open, if it survived the refresh.
        int row = 0;
        for (int i = 0; i < list_->count(); ++i) {
            if (list_->item(i)->data(Qt::UserRole).toString().toStdString() == keep) {
                row = i;
                break;
            }
        }
        list_->setCurrentRow(row);
    } else {
        current_.clear();
    }
    Q_EMIT noteCountChanged(static_cast<int>(notes.size()));
    refreshReminders();
}

void NotesView::refreshReminders() {
    if (reminderRows_ == nullptr) return;
    QLayoutItem* item = nullptr;
    while ((item = reminderRows_->takeAt(0)) != nullptr) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    const auto pending = brain::tools::pending_reminders();
    reminderPanel_->setVisible(!pending.empty());
    if (pending.empty()) return;

    auto* heading = new QLabel(QStringLiteral("REMINDERS"));
    heading->setStyleSheet(
        QStringLiteral("color: %1; font-size: 11px; font-weight: 700;"
                       "letter-spacing: 1.4px; background: transparent;")
            .arg(theme::kFaint.name()));
    reminderRows_->addWidget(heading);

    for (const auto& reminder : pending) {
        auto* row = new QWidget;
        // Scoped by object name: an unqualified rule on a container is
        // inherited by its children, so the label inside drew its own border
        // too and every row came out doubled.
        row->setObjectName(QStringLiteral("reminderRow"));
        row->setStyleSheet(QStringLiteral("#reminderRow { background: %1;"
                                          "  border: 1px solid %2; border-radius: 8px; }")
                               .arg(theme::kLayer1.name(), theme::kLine.name()));
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(12, 8, 8, 8);
        line->setSpacing(8);

        auto* when = new QLabel(QString::fromStdString(reminder.when));
        when->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;"
                                           "background: transparent; border: none;")
                                .arg(theme::kAccentSoft.name()));
        line->addWidget(when);

        auto* what = new QLabel(QString::fromStdString(reminder.what));
        what->setStyleSheet(QStringLiteral("color: %1; font-size: 14px;"
                                           "background: transparent; border: none;")
                                .arg(theme::kInk.name()));
        line->addWidget(what, 1);

        auto* drop = new QPushButton(QStringLiteral("✕"));
        drop->setCursor(Qt::PointingHandCursor);
        drop->setFixedSize(22, 22);
        drop->setToolTip(QStringLiteral("Cancel this reminder"));
        drop->setStyleSheet(
            QStringLiteral("QPushButton { background: transparent; border: none;"
                           "  color: %1; font-size: 13px; }"
                           "QPushButton:hover { color: %2; }")
                .arg(theme::kFaint.name(), theme::kError.name()));
        const std::string what_text = reminder.what;
        connect(drop, &QPushButton::clicked, this, [this, what_text] {
            brain::tools::cancel_reminder(what_text);
            refreshReminders();
        });
        line->addWidget(drop);
        reminderRows_->addWidget(row);
    }
}

void NotesView::showSelected() {
    QListWidgetItem* item = list_->currentItem();
    if (item == nullptr) return;
    const std::string id = item->data(Qt::UserRole).toString().toStdString();
    const brain::Note note = notes_.get(id);
    if (!note.valid()) return;

    // setPlainText fires textChanged; without this the load would immediately
    // write the note back over itself.
    loading_ = true;
    current_ = note.id;
    body_->setPlainText(QString::fromStdString(note.body));
    stamp_->setText(QString::fromStdString(note.created).replace('T', ' '));
    loading_ = false;
}

void NotesView::saveCurrent() {
    if (current_.empty() || loading_) return;
    const std::string body = body_->toPlainText().toStdString();
    if (!notes_.update(current_, body)) return;

    // The title comes from the first line, so the index can go stale on edit.
    const brain::Note note = notes_.get(current_);
    if (QListWidgetItem* item = list_->currentItem();
        item != nullptr && note.valid()) {
        item->setText(QString::fromStdString(note.title));
    }
}

void NotesView::addNote() {
    const brain::Note note = notes_.add("New note");
    if (!note.valid()) return;
    current_ = note.id;
    refresh();
    body_->setFocus();
    body_->selectAll();
}

void NotesView::deleteSelected() {
    if (current_.empty()) return;
    notes_.remove(current_);
    current_.clear();
    refresh();
}

}  // namespace mimi::ui
