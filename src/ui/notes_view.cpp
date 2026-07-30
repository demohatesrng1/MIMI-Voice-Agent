#include "ui/notes_view.hpp"

#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace mimi::ui {
namespace {

QString stylesheet() {
    const auto hex = [](const QColor& colour) { return colour.name(QColor::HexRgb); };
    return QStringLiteral(
               "QListWidget { background: %1; border: none; border-radius: 12px;"
               "  padding: 6px; color: %2; }"
               "QListWidget::item { padding: 11px 12px; border-radius: 8px; }"
               "QListWidget::item:selected { background: %3; color: %4; }"
               "QTextEdit { background: %1; border: none; border-radius: 12px;"
               "  padding: 16px; color: %2; selection-background-color: %3; }")
        .arg(hex(theme::kLayer1), hex(theme::kInk), hex(theme::kAccentDeep),
             hex(theme::kInk));
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
    heading->setStyleSheet(QStringLiteral("color: %1; font-size: 22px; font-weight: 600;")
                               .arg(theme::kInk.name()));
    left->addWidget(heading);

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

    const auto notes = notes_.all();
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
