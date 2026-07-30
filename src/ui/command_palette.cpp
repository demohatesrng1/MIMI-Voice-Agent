#include "ui/command_palette.hpp"

#include "ui/theme.hpp"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

#include <array>

namespace mimi::ui {
namespace {

// A command: what it reads as, its category, and what it does. navPage >= 0
// jumps to a surface (the Page enum); otherwise the utterance runs through the
// router exactly as if spoken.
struct Command {
    const char* title;
    const char* category;
    int navPage;
    const char* utterance;
};

constexpr std::array<Command, 10> kCommands{{
    {"Take a screenshot", "Action", -1, "スクリーンショットを撮って"},
    {"Lock the screen", "Action", -1, "画面をロックして"},
    {"Battery status", "Ask", -1, "バッテリーはどのくらい"},
    {"What time is it?", "Ask", -1, "今何時ですか"},
    {"Summarize the meeting", "Ask", -1, "会議の内容を要約して"},
    {"Go Home", "Navigate", 0, ""},
    {"Open Canvas", "Navigate", 1, ""},
    {"Open Memory timeline", "Navigate", 2, ""},
    {"Open Missions", "Navigate", 3, ""},
    {"Open Settings", "Navigate", 4, ""},
}};

constexpr int kRoleIndex = Qt::UserRole;  // stores the command index on each row

}  // namespace

CommandPalette::CommandPalette(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    hide();

    // The palette sits in the upper third, centred, on a dimmed scrim.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addSpacing(120);
    auto* rowHolder = new QWidget;
    auto* row = new QHBoxLayout(rowHolder);
    row->setContentsMargins(0, 0, 0, 0);
    row->addStretch(1);

    panel_ = new QWidget;
    panel_->setObjectName(QStringLiteral("palettePanel"));
    panel_->setFixedWidth(560);
    auto* panelCol = new QVBoxLayout(panel_);
    panelCol->setContentsMargins(0, 0, 0, 8);
    panelCol->setSpacing(0);

    field_ = new QLineEdit;
    field_->setObjectName(QStringLiteral("paletteField"));
    field_->setPlaceholderText(QStringLiteral("Type a command…  ⌘K"));
    field_->installEventFilter(this);
    panelCol->addWidget(field_);

    list_ = new QListWidget;
    list_->setObjectName(QStringLiteral("paletteList"));
    list_->setFocusPolicy(Qt::NoFocus);  // keys are routed from the field
    list_->setMaximumHeight(360);
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        activateCurrent();
    });
    panelCol->addWidget(list_);

    row->addWidget(panel_);
    row->addStretch(1);
    outer->addWidget(rowHolder);
    outer->addStretch(1);

    connect(field_, &QLineEdit::textChanged, this, &CommandPalette::refilter);
}

void CommandPalette::open() {
    if (parentWidget() != nullptr) {
        resize(parentWidget()->size());
        move(0, 0);
    }
    field_->clear();
    refilter(QString());
    show();
    raise();
    field_->setFocus();
}

void CommandPalette::dismiss() { hide(); }

void CommandPalette::refilter(const QString& query) {
    list_->clear();
    for (int i = 0; i < static_cast<int>(kCommands.size()); ++i) {
        const Command& c = kCommands[i];
        const QString title = QString::fromUtf8(c.title);
        if (!query.isEmpty() && !title.contains(query, Qt::CaseInsensitive)) continue;
        auto* item = new QListWidgetItem(
            QStringLiteral("%1      %2").arg(title, QString::fromUtf8(c.category)));
        item->setData(kRoleIndex, i);
        list_->addItem(item);
    }
    if (list_->count() > 0) list_->setCurrentRow(0);
}

void CommandPalette::activateCurrent() {
    QListWidgetItem* item = list_->currentItem();
    if (item == nullptr) return;
    const Command& c = kCommands[item->data(kRoleIndex).toInt()];
    dismiss();
    if (c.navPage >= 0)
        Q_EMIT navigateChosen(c.navPage);
    else
        Q_EMIT commandChosen(QString::fromUtf8(c.utterance));
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event) {
    if (watched == field_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
            case Qt::Key_Down:
                list_->setCurrentRow(qMin(list_->currentRow() + 1, list_->count() - 1));
                return true;
            case Qt::Key_Up:
                list_->setCurrentRow(qMax(list_->currentRow() - 1, 0));
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                activateCurrent();
                return true;
            case Qt::Key_Escape:
                dismiss();
                return true;
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CommandPalette::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CommandPalette::mousePressEvent(QMouseEvent* event) {
    // A click on the scrim, outside the panel, closes the palette.
    const QPoint inPanel = panel_->mapFrom(this, event->pos());
    if (!panel_->rect().contains(inPanel)) {
        dismiss();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CommandPalette::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    // Dim the workspace behind, so the palette is unmistakably the focus.
    QColor scrim = theme::kVoid;
    scrim.setAlpha(150);
    painter.fillRect(rect(), scrim);
}

}  // namespace mimi::ui
