#include "ui/chat_view.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace mimi::ui {
namespace {

QString class_for(Speaker who) {
    switch (who) {
        case Speaker::You:    return QStringLiteral("bubbleYou");
        case Speaker::Mimi:   return QStringLiteral("bubbleMimi");
        case Speaker::System: return QStringLiteral("bubbleSystem");
    }
    return QStringLiteral("bubbleMimi");
}

}  // namespace

ChatView::ChatView(QWidget* parent) : QScrollArea(parent) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    content_ = new QWidget;
    content_->setObjectName(QStringLiteral("chatContent"));
    layout_ = new QVBoxLayout(content_);
    layout_->setContentsMargins(22, 20, 22, 20);
    layout_->setSpacing(12);
    layout_->addStretch(1);  // keeps bubbles pinned to the bottom
    setWidget(content_);
}

int ChatView::append(Speaker who, const QString& text) {
    auto* row = new QWidget(content_);
    row->setProperty("chatId", next_id_);

    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);

    auto* bubble = new QLabel(text, row);
    bubble->setObjectName(class_for(who));
    bubble->setWordWrap(true);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bubble->setMaximumWidth(520);

    if (who == Speaker::You) {
        row_layout->addStretch(1);
        row_layout->addWidget(bubble);
    } else if (who == Speaker::System) {
        row_layout->addStretch(1);
        row_layout->addWidget(bubble);
        row_layout->addStretch(1);
    } else {
        row_layout->addWidget(bubble);
        row_layout->addStretch(1);
    }

    // Insert before the trailing stretch.
    layout_->insertWidget(layout_->count() - 1, row);
    scrollToBottom();
    return next_id_++;
}

void ChatView::replace(int id, const QString& text) {
    for (int i = 0; i < layout_->count(); ++i) {
        QWidget* row = layout_->itemAt(i)->widget();
        if (row == nullptr || row->property("chatId").toInt() != id) continue;
        if (auto* bubble = row->findChild<QLabel*>()) {
            bubble->setText(text);
            scrollToBottom();
        }
        return;
    }
}

void ChatView::remove(int id) {
    for (int i = 0; i < layout_->count(); ++i) {
        QWidget* row = layout_->itemAt(i)->widget();
        if (row == nullptr || row->property("chatId").toInt() != id) continue;
        layout_->removeWidget(row);
        row->deleteLater();
        return;
    }
}

void ChatView::clear() {
    while (layout_->count() > 1) {
        QLayoutItem* item = layout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void ChatView::scrollToBottom() {
    // Deferred: the layout has not been recomputed yet at this point, so the
    // scrollbar maximum is still the old one.
    QTimer::singleShot(0, this, [this] {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    });
}

}  // namespace mimi::ui
