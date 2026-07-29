#include "ui/command_bar.hpp"

#include "ui/controls.hpp"
#include "ui/theme.hpp"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QVariantAnimation>

namespace mimi::ui {

CommandBar::CommandBar(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedHeight(54);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(24, 0, 12, 0);
    layout->setSpacing(8);

    field_ = new QLineEdit;
    field_->setObjectName(QStringLiteral("commandField"));
    field_->setPlaceholderText(QStringLiteral("Ask Mimi anything…"));
    field_->setFrame(false);
    field_->installEventFilter(this);
    {
        QPalette palette = field_->palette();
        palette.setColor(QPalette::PlaceholderText, theme::kFaint);
        field_->setPalette(palette);
    }
    connect(field_, &QLineEdit::returnPressed, this, [this] {
        const QString text = field_->text().trimmed();
        if (text.isEmpty()) return;
        field_->clear();
        Q_EMIT submitted(text);
    });
    layout->addWidget(field_, 1);

    mic_ = new GhostButton(icons::Glyph::Mic);
    mic_->setToolTip(QStringLiteral("Speak now — no wake word needed"));
    connect(mic_, &GhostButton::clicked, this, &CommandBar::micClicked);
    layout->addWidget(mic_);

    // The shadow is the elevation. It deepens with focus, so the bar visibly
    // rises toward you when it becomes the active object.
    shadow_ = new QGraphicsDropShadowEffect(this);
    shadow_->setColor(QColor(0, 0, 0, 150));
    shadow_->setBlurRadius(28);
    shadow_->setOffset(0, 10);
    setGraphicsEffect(shadow_);

    anim_ = new QVariantAnimation(this);
    anim_->setDuration(theme::kMotionMs);
    anim_->setEasingCurve(theme::kMotion);
    connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        focus_ = v.toReal();
        shadow_->setBlurRadius(28 + 16 * focus_);
        shadow_->setOffset(0, 10 + 4 * focus_);
        update();
    });
}

void CommandBar::setMicEnabled(bool enabled) { mic_->setEnabled(enabled); }

bool CommandBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == field_ &&
        (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)) {
        anim_->stop();
        anim_->setStartValue(focus_);
        anim_->setEndValue(event->type() == QEvent::FocusIn ? 1.0 : 0.0);
        anim_->start();
    }
    return QWidget::eventFilter(watched, event);
}

void CommandBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF body = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = body.height() / 2.0;

    // Glass: a raised layer with its own slight translucency, so the ambient
    // light behind it stays part of the picture.
    QColor glass = theme::kLayer2;
    glass.setAlpha(216);
    painter.setPen(Qt::NoPen);
    painter.setBrush(glass);
    painter.drawRoundedRect(body, radius, radius);

    // Rim: barely-there white at rest -- the lit top edge of a physical
    // object -- shifting toward the accent as focus arrives.
    QColor rim(255, 255, 255, 18);
    if (focus_ > 0.01) {
        QColor accent = theme::kAccent;
        accent.setAlphaF(0.10 + 0.35 * focus_);
        rim = accent;
    }
    painter.setPen(QPen(rim, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(body, radius, radius);

    // Top highlight: light falls from above.
    QPainterPath clip;
    clip.addRoundedRect(body.adjusted(1, 1, -1, -1), radius - 1, radius - 1);
    painter.setClipPath(clip);
    painter.setPen(QPen(QColor(255, 255, 255, 14), 1.0));
    painter.drawLine(QPointF(body.left() + radius * 0.8, body.top() + 1.5),
                     QPointF(body.right() - radius * 0.8, body.top() + 1.5));
}

}  // namespace mimi::ui
