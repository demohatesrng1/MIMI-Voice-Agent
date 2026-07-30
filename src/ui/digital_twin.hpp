#pragma once

#include <QWidget>

namespace mimi::ui {

// Digital Twin: what she has learned about you, made visible. Your style, your
// rhythms, your projects and people -- the beginnings of a second brain. Seeded
// for now; each card fills from real signals later.
class DigitalTwin : public QWidget {
    Q_OBJECT

public:
    explicit DigitalTwin(QWidget* parent = nullptr);
};

}  // namespace mimi::ui
