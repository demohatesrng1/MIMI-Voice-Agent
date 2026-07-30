#pragma once

#include <QString>
#include <QWidget>

namespace mimi::ui {

// Predictive actions: the assistant stops waiting to be asked and offers the
// next move. A row of quiet cards -- "Continue yesterday's report?",
// "Summarize this week?" -- each phrased as a question because it is a guess,
// and each one a real command if you take it. Shown when she is idle and
// observing; it is the surface where anticipation lives.
class PredictiveActions : public QWidget {
    Q_OBJECT

public:
    explicit PredictiveActions(QWidget* parent = nullptr);

Q_SIGNALS:
    // A prediction was accepted; the text runs through the router like any
    // other command.
    void commandRequested(QString utterance);
};

}  // namespace mimi::ui
