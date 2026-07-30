#pragma once

#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

namespace mimi::ui {

// The context ribbon: a thin strip under the chrome that says, at all times,
// what Mimi understands the work to be. Current task on the left; the things
// she has connected to it -- files, related mail, people, how much memory she
// is holding -- as quiet metrics on the right. It is how the app signals that
// it always knows the context, without the user having to ask.
class ContextRibbon : public QWidget {
    Q_OBJECT

public:
    explicit ContextRibbon(QWidget* parent = nullptr);

    QSize sizeHint() const override;

    void setTask(const QString& task);
    void setMetric(const QString& label, const QString& value);
    // Simple mode shows only the task; Expert mode shows the full metric set.
    void setCompact(bool compact);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString task_;
    QVector<QPair<QString, QString>> metrics_;  // label -> value, drawn right to left
    bool compact_ = false;
};

}  // namespace mimi::ui
