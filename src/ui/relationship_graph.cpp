#include "ui/relationship_graph.hpp"

#include "ui/theme.hpp"

#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

#include <array>

namespace mimi::ui {
namespace {
constexpr std::array<const char*, 6> kNodes{
    {"Projects", "Meetings", "Documents", "Mail", "People", "Tasks"}};
}

RelationshipGraph::RelationshipGraph(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("graph"));
}

void RelationshipGraph::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(width() / 2.0, height() / 2.0);
    const qreal r = std::min(width(), height()) * 0.30;

    // Edges first.
    for (int i = 0; i < static_cast<int>(kNodes.size()); ++i) {
        const qreal a = i * (2.0 * M_PI / kNodes.size()) - M_PI_2;
        const QPointF n(c.x() + std::cos(a) * r, c.y() + std::sin(a) * r);
        QColor line = theme::kAccent;
        line.setAlphaF(0.28);
        p.setPen(QPen(line, 1.4));
        p.drawLine(c, n);
    }

    // Outer nodes.
    QFont f = font();
    f.setPixelSize(12);
    f.setWeight(QFont::DemiBold);
    for (int i = 0; i < static_cast<int>(kNodes.size()); ++i) {
        const qreal a = i * (2.0 * M_PI / kNodes.size()) - M_PI_2;
        const QPointF n(c.x() + std::cos(a) * r, c.y() + std::sin(a) * r);
        QColor glass = theme::kLayer2;
        glass.setAlpha(238);
        p.setPen(Qt::NoPen);
        p.setBrush(glass);
        p.drawEllipse(n, 46, 46);
        p.setPen(QPen(QColor(255, 255, 255, 22), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(n, 46, 46);
        p.setFont(f);
        p.setPen(theme::kInk);
        p.drawText(QRectF(n.x() - 46, n.y() - 10, 92, 20), Qt::AlignCenter,
                   QString::fromUtf8(kNodes[i]));
    }

    // Centre node: the client, brightest.
    QRadialGradient g(c, 62);
    g.setColorAt(0.0, theme::kAccentDeep);
    g.setColorAt(1.0, theme::kLayer1);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(c, 58, 58);
    QColor rim = theme::kAccent;
    rim.setAlphaF(0.8);
    p.setPen(QPen(rim, 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, 58, 58);
    QFont cf = font();
    cf.setPixelSize(15);
    cf.setWeight(QFont::DemiBold);
    p.setFont(cf);
    p.setPen(theme::kInk);
    p.drawText(QRectF(c.x() - 58, c.y() - 12, 116, 24), Qt::AlignCenter,
               QStringLiteral("Acme Corp"));
}

}  // namespace mimi::ui
