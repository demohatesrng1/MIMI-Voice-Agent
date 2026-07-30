#pragma once

#include <QWidget>

namespace mimi::ui {

// The AI dock: a floating glass rail of the assistant's faculties -- voice,
// chat, vision, files, browser, automation, memory -- always in reach, no
// matter which surface you are on. It hovers over the workspace on its own
// shadow rather than living in a bar, so it reads as the assistant being
// present everywhere rather than a toolbar you navigate to.
class AiDock : public QWidget {
    Q_OBJECT

public:
    enum Item { Voice, Chat, Vision, Files, Browser, Automation, Memory };

    explicit AiDock(QWidget* parent = nullptr);

Q_SIGNALS:
    void itemSelected(int item);  // AiDock::Item

protected:
    void paintEvent(QPaintEvent*) override;
};

}  // namespace mimi::ui
