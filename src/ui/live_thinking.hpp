#pragma once

#include <QVector>
#include <QWidget>

class QTimer;

namespace mimi::ui {

// Live thinking, instead of a spinner.
//
// "Loading…" tells you nothing and reads as a hang. This shows the work: a
// short pipeline of stages -- understanding, finding context, building,
// checking -- that fill in sequence while she reasons. If the answer lands
// early the pipeline snaps complete; if she takes longer, the last stage keeps
// breathing so waiting still reads as progress rather than a stall.
class LiveThinking : public QWidget {
    Q_OBJECT

public:
    explicit LiveThinking(QWidget* parent = nullptr);

    QSize sizeHint() const override;

    void start();   // reset and begin advancing through the stages
    void finish();  // the answer arrived: complete every stage at once
    void stop();    // halt the clock (e.g. when hidden)

protected:
    void paintEvent(QPaintEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    QTimer* tick_ = nullptr;
    int stage_ = 0;     // the stage currently filling
    qreal fill_ = 0.0;  // 0..1 within the active stage
    qreal phase_ = 0.0;  // shimmer clock for the active bar
    bool done_ = false;
};

}  // namespace mimi::ui
