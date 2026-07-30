#pragma once

#include <QString>
#include <QWidget>

namespace mimi::ui {

// Ambient intelligence: she notices things and mentions them, unprompted -- "I
// noticed you've rewritten this three times." A quiet glass pill that surfaces
// an observation without demanding a response.
class AmbientNotice : public QWidget {
    Q_OBJECT

public:
    explicit AmbientNotice(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    void notice(const QString& text);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString text_;
};

}  // namespace mimi::ui
