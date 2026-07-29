#pragma once

#include <QScrollArea>
#include <QString>

class QVBoxLayout;
class QWidget;

namespace mimi::ui {

enum class Speaker { You, Mimi, System };

// A scrolling transcript of the conversation.
//
// Deliberately not a QTextEdit: each turn is its own widget so bubbles can be
// styled independently, and so a pending turn can be replaced in place when the
// real answer arrives.
class ChatView : public QScrollArea {
    Q_OBJECT

public:
    explicit ChatView(QWidget* parent = nullptr);

    // Returns an id that can be passed to replace() -- used for the "…" bubble
    // shown while Mimi is thinking.
    int append(Speaker who, const QString& text);
    void replace(int id, const QString& text);
    void remove(int id);
    void clear();

private:
    void scrollToBottom();

    QWidget* content_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    int next_id_ = 1;
};

}  // namespace mimi::ui
