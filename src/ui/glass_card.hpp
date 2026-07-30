#pragma once

#include <QPointF>
#include <QString>
#include <QWidget>

class QGraphicsDropShadowEffect;
class QVariantAnimation;

namespace mimi::ui {

// Spatial UI, distilled into one reusable object: a card that is unmistakably a
// physical thing. It rests on a soft shadow; as the cursor arrives it lifts, a
// specular highlight slides across the glass toward the pointer, and the shadow
// shifts the other way -- parallax, so depth is felt rather than outlined.
// Nothing here is flat.
class GlassCard : public QWidget {
    Q_OBJECT

public:
    GlassCard(const QString& tag, const QString& title, const QString& body,
              QWidget* parent = nullptr);

    QSize sizeHint() const override { return {560, 100}; }

    void setBody(const QString& body);

Q_SIGNALS:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void animateHover(qreal to);

    QString tag_;
    QString title_;
    QString body_;
    qreal hover_ = 0.0;
    QPointF light_{0.0, -0.3};  // cursor position, normalised to -1..1
    QGraphicsDropShadowEffect* shadow_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
};

}  // namespace mimi::ui
