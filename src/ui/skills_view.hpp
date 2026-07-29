#pragma once

#include <QScrollArea>
#include <QString>

namespace mimi::ui {

// What she can do, as a browsable catalogue.
//
// The hardest problem with a voice product is that its capabilities are
// invisible: nothing on screen tells you the sentence that works. This page
// answers that directly -- every skill, grouped, with a phrase you can say
// verbatim or click to run.
class SkillsView : public QScrollArea {
    Q_OBJECT

public:
    explicit SkillsView(QWidget* parent = nullptr);

Q_SIGNALS:
    void commandRequested(QString utterance);
};

}  // namespace mimi::ui
