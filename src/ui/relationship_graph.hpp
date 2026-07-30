#pragma once

#include <QWidget>

namespace mimi::ui {

// Relationship Graph: folders replaced by connections. A client at the centre,
// with its projects, meetings, documents, mail, people and tasks orbiting and
// linked. Seeded and static for now.
class RelationshipGraph : public QWidget {
    Q_OBJECT

public:
    explicit RelationshipGraph(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
};

}  // namespace mimi::ui
