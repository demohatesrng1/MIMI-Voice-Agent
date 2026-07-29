#pragma once

#include <QScrollArea>
#include <QString>

class QVBoxLayout;

namespace mimi::ui {

// The record of what has happened.
//
// This is where history lives, as a log rather than a conversation: one row
// per exchange, newest first, with the time, what was said, what she answered,
// and a badge when she actually changed something on the machine. Scanning
// "what did she do" is a different job from reading a chat, and wants a
// different shape.
class ActivityView : public QScrollArea {
    Q_OBJECT

public:
    explicit ActivityView(QWidget* parent = nullptr);

    // `action` is the router's verb; `acted` is whether the machine changed.
    void record(const QString& said, const QString& replied, const QString& action,
                bool acted);
    void clear();

private:
    QWidget* content_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    QWidget* empty_ = nullptr;
    int count_ = 0;
};

}  // namespace mimi::ui
