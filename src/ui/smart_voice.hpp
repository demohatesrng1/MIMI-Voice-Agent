#pragma once

#include <QWidget>

namespace mimi::ui {

// Smart Voice: a quiet strip that says talking here is like talking to a
// person -- interrupt, pause, resume, all understood. The engine already does
// barge-in; this makes the capability visible.
class SmartVoiceBar : public QWidget {
    Q_OBJECT

public:
    explicit SmartVoiceBar(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {360, 26}; }

protected:
    void paintEvent(QPaintEvent*) override;
};

}  // namespace mimi::ui
