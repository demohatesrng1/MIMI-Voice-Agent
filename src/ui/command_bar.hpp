#pragma once

#include <QWidget>

class QGraphicsDropShadowEffect;
class QLineEdit;
class QVariantAnimation;

namespace mimi::ui {

class GhostButton;

// The command bar: a single floating glass capsule, and the centre of the
// product. It does not touch the window edges -- it hovers on its own shadow,
// which is what makes it read as an object you speak to rather than a form
// field at the bottom of a page. Focus deepens the shadow and warms the rim
// with the accent, on the same 180 ms clock as everything else.
class CommandBar : public QWidget {
    Q_OBJECT

public:
    explicit CommandBar(QWidget* parent = nullptr);

    QLineEdit* field() const { return field_; }
    void setMicEnabled(bool enabled);

Q_SIGNALS:
    void submitted(const QString& text);
    void micClicked();

protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLineEdit* field_ = nullptr;
    GhostButton* mic_ = nullptr;
    QGraphicsDropShadowEffect* shadow_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
    qreal focus_ = 0.0;
};

}  // namespace mimi::ui
