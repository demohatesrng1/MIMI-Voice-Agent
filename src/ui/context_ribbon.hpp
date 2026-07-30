#pragma once

#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

namespace mimi::ui {

// The context ribbon: a thin strip under the chrome showing the things Mimi has
// connected to the work -- how many notes she is holding, whether she can drive
// other apps -- as quiet metrics. It is how the app signals what it knows,
// without the user having to ask.
class ContextRibbon : public QWidget {
    Q_OBJECT

public:
    explicit ContextRibbon(QWidget* parent = nullptr);

    QSize sizeHint() const override;

    void setMetric(const QString& label, const QString& value);
    void setCompact(bool compact);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVector<QPair<QString, QString>> metrics_;  // label -> value, drawn right to left
    bool compact_ = false;
};

}  // namespace mimi::ui
