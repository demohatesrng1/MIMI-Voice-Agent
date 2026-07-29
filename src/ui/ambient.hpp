#pragma once

#include <QWidget>

class QTimer;

namespace mimi::ui {

// The bottom layer of the interface: a graphite surface lit by two very slow
// pools of blue light. The drift is glacial on purpose -- at a glance the
// background looks still, but it is never *frozen*, which is most of what
// separates a live product from a screenshot of one. Painted with alpha so
// the native vibrancy underneath still contributes real depth.
class AmbientCanvas : public QWidget {
    Q_OBJECT

public:
    explicit AmbientCanvas(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QTimer* tick_ = nullptr;
    qreal phase_ = 0.0;
};

}  // namespace mimi::ui
