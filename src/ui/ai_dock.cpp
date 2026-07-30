#include "ui/ai_dock.hpp"

#include "ui/controls.hpp"
#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QVBoxLayout>

#include <array>

namespace mimi::ui {
namespace {

struct Faculty {
    icons::Glyph glyph;
    const char* tip;
};

const std::array<Faculty, 7> kFaculties{{
    {icons::Glyph::Mic, "Voice"},
    {icons::Glyph::Chat, "Chat"},
    {icons::Glyph::Vision, "Vision"},
    {icons::Glyph::Files, "Files"},
    {icons::Glyph::Browser, "Browser"},
    {icons::Glyph::Automation, "Automation"},
    {icons::Glyph::Memory, "Memory"},
}};

}  // namespace

AiDock::AiDock(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(7, 10, 7, 10);
    column->setSpacing(4);

    for (int i = 0; i < static_cast<int>(kFaculties.size()); ++i) {
        auto* button = new GhostButton(kFaculties[i].glyph);
        button->setToolTip(QString::fromUtf8(kFaculties[i].tip));
        connect(button, &GhostButton::clicked, this, [this, i] { Q_EMIT itemSelected(i); });
        column->addWidget(button, 0, Qt::AlignHCenter);
    }

    // It floats: the shadow is the distance between it and the workspace.
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setColor(QColor(0, 0, 0, 160));
    shadow->setBlurRadius(30);
    shadow->setOffset(0, 12);
    setGraphicsEffect(shadow);
}

void AiDock::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF body = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = body.width() / 2.0;

    // Glass, slightly translucent so the living background stays part of it.
    QColor glass = theme::kLayer2;
    glass.setAlpha(220);
    painter.setPen(Qt::NoPen);
    painter.setBrush(glass);
    painter.drawRoundedRect(body, radius, radius);

    // Lit top edge, like every raised object in the app.
    painter.setPen(QPen(QColor(255, 255, 255, 20), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(body, radius, radius);
}

}  // namespace mimi::ui
