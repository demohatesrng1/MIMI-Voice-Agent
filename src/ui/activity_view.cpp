#include "ui/activity_view.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QVBoxLayout>

namespace mimi::ui {

ActivityView::ActivityView(QWidget* parent) : QScrollArea(parent) {
    setObjectName(QStringLiteral("activity"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    content_ = new QWidget;
    content_->setObjectName(QStringLiteral("activityContent"));
    layout_ = new QVBoxLayout(content_);
    layout_->setContentsMargins(30, 26, 30, 26);
    layout_->setSpacing(8);

    auto* heading = new QLabel(QStringLiteral("ACTIVITY"));
    heading->setObjectName(QStringLiteral("sectionHead"));
    layout_->addWidget(heading);
    layout_->addSpacing(6);

    empty_ = new QLabel(QStringLiteral("Nothing yet.\n\nEverything you ask will be recorded here."));
    empty_->setObjectName(QStringLiteral("emptyState"));
    layout_->addWidget(empty_);

    layout_->addStretch(1);
    setWidget(content_);
}

void ActivityView::record(const QString& said, const QString& replied,
                          const QString& action, bool acted) {
    if (empty_ != nullptr) {
        empty_->hide();
        empty_ = nullptr;
    }

    auto* row = new QWidget;
    row->setObjectName(QStringLiteral("activityRow"));
    auto* outer = new QHBoxLayout(row);
    outer->setContentsMargins(16, 12, 16, 12);
    outer->setSpacing(14);

    auto* time = new QLabel(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")));
    time->setObjectName(QStringLiteral("rowTime"));
    time->setFixedWidth(38);
    time->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    outer->addWidget(time);

    auto* text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(3);

    auto* saidLabel = new QLabel(said);
    saidLabel->setObjectName(QStringLiteral("rowSaid"));
    saidLabel->setWordWrap(true);
    text->addWidget(saidLabel);

    auto* replyLabel = new QLabel(replied);
    replyLabel->setObjectName(QStringLiteral("rowReply"));
    replyLabel->setWordWrap(true);
    text->addWidget(replyLabel);

    outer->addLayout(text, 1);

    // Only shown when she actually changed something. A badge on every row
    // would carry no information at all.
    if (acted && !action.isEmpty()) {
        auto* badge = new QLabel(action);
        badge->setObjectName(QStringLiteral("rowBadge"));
        badge->setAlignment(Qt::AlignTop);
        outer->addWidget(badge, 0, Qt::AlignTop);
    }

    // Newest first: index 2 clears the heading and its spacer.
    layout_->insertWidget(2, row);
    ++count_;

    verticalScrollBar()->setValue(0);
}

void ActivityView::clear() {
    while (layout_->count() > 3) {
        QLayoutItem* item = layout_->takeAt(2);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    count_ = 0;
}

}  // namespace mimi::ui
