#include "ui/icons.hpp"

#include <QColor>
#include <QHash>
#include <QPainter>
#include <QPixmap>

#include <cmath>

namespace mimi::ui::icons {
namespace {

// Everything is authored on a 24x24 grid with a 2px stroke, so the icons share
// an optical weight no matter what size they end up drawn at.
constexpr qreal kGrid = 24.0;
constexpr qreal kStroke = 1.9;

QPainterPath build(Glyph glyph) {
    QPainterPath p;
    switch (glyph) {
        case Glyph::Mic:
            // Capsule, stand, base. A real microphone outline rather than the
            // emoji, which is a studio mic on a boom and reads as clutter.
            p.addRoundedRect(QRectF(9, 3, 6, 11), 3, 3);
            p.moveTo(5.5, 11.5);
            p.arcTo(QRectF(5.5, 5.5, 13, 13), 180, 180);
            p.moveTo(12, 18.5);
            p.lineTo(12, 21);
            p.moveTo(8.5, 21);
            p.lineTo(15.5, 21);
            break;

        case Glyph::Home:
            p.moveTo(4, 10.5);
            p.lineTo(12, 4);
            p.lineTo(20, 10.5);
            p.lineTo(20, 19);
            p.lineTo(4, 19);
            p.closeSubpath();
            break;

        case Glyph::Activity:
            // Three rules of unequal length: a list, not a hamburger menu.
            p.moveTo(4.5, 7);   p.lineTo(19.5, 7);
            p.moveTo(4.5, 12);  p.lineTo(16, 12);
            p.moveTo(4.5, 17);  p.lineTo(19.5, 17);
            break;

        case Glyph::Skills: {
            // Four-point star.
            const QPointF c(12, 12);
            p.moveTo(c.x(), c.y() - 8);
            p.quadTo(c.x() + 1.6, c.y() - 1.6, c.x() + 8, c.y());
            p.quadTo(c.x() + 1.6, c.y() + 1.6, c.x(), c.y() + 8);
            p.quadTo(c.x() - 1.6, c.y() + 1.6, c.x() - 8, c.y());
            p.quadTo(c.x() - 1.6, c.y() - 1.6, c.x(), c.y() - 8);
            break;
        }

        case Glyph::Canvas:
            // A board with cards on it: an outer frame and two offset tiles,
            // for the infinite canvas. Reads as a workspace, not a document.
            p.addRoundedRect(QRectF(3.5, 4.5, 17, 15), 2.4, 2.4);
            p.addRoundedRect(QRectF(6, 7, 5.5, 4), 1.2, 1.2);
            p.addRoundedRect(QRectF(13, 11, 5, 5.5), 1.2, 1.2);
            break;

        case Glyph::Timeline:
            // A spine with three nodes and their ticks -- events threaded in
            // time, top to bottom.
            p.moveTo(7, 4.5);
            p.lineTo(7, 19.5);
            p.addEllipse(QPointF(7, 7), 1.5, 1.5);
            p.addEllipse(QPointF(7, 12), 1.5, 1.5);
            p.addEllipse(QPointF(7, 17), 1.5, 1.5);
            p.moveTo(10, 7);   p.lineTo(18, 7);
            p.moveTo(10, 12);  p.lineTo(16, 12);
            p.moveTo(10, 17);  p.lineTo(18, 17);
            break;

        case Glyph::Chat:
            // A speech bubble with a tail: conversation.
            p.addRoundedRect(QRectF(3.5, 4.5, 17, 11), 3, 3);
            p.moveTo(8.5, 15.5);
            p.lineTo(7.5, 19.5);
            p.lineTo(12.5, 15.5);
            break;

        case Glyph::Vision:
            // An eye: an almond formed by two curves, with an iris.
            p.moveTo(3.5, 12);
            p.quadTo(12, 5, 20.5, 12);
            p.quadTo(12, 19, 3.5, 12);
            p.closeSubpath();
            p.addEllipse(QPointF(12, 12), 2.7, 2.7);
            break;

        case Glyph::Files:
            // A document with a folded corner.
            p.moveTo(6, 3.5);
            p.lineTo(14, 3.5);
            p.lineTo(18.5, 8);
            p.lineTo(18.5, 20.5);
            p.lineTo(6, 20.5);
            p.closeSubpath();
            p.moveTo(14, 3.5);
            p.lineTo(14, 8);
            p.lineTo(18.5, 8);
            break;

        case Glyph::Browser:
            // A globe: a circle crossed by an equator and a meridian.
            p.addEllipse(QPointF(12, 12), 8.3, 8.3);
            p.moveTo(3.7, 12);
            p.lineTo(20.3, 12);
            p.moveTo(12, 3.7);
            p.quadTo(6.2, 12, 12, 20.3);
            p.moveTo(12, 3.7);
            p.quadTo(17.8, 12, 12, 20.3);
            break;

        case Glyph::Automation:
            // A lightning bolt: work that runs itself.
            p.moveTo(13, 3.5);
            p.lineTo(6.5, 13);
            p.lineTo(11, 13);
            p.lineTo(10, 20.5);
            p.lineTo(17, 10.5);
            p.lineTo(12.5, 10.5);
            p.closeSubpath();
            break;

        case Glyph::Memory:
            // A database cylinder: stored knowledge.
            p.addEllipse(QRectF(5, 4, 14, 5));
            p.moveTo(5, 6.5);
            p.lineTo(5, 17.5);
            p.quadTo(12, 20.5, 19, 17.5);
            p.lineTo(19, 6.5);
            p.moveTo(5, 12);
            p.quadTo(12, 15, 19, 12);
            break;

        case Glyph::Mission:
            // A target: an objective to lock onto.
            p.addEllipse(QPointF(12, 12), 8.3, 8.3);
            p.addEllipse(QPointF(12, 12), 4.3, 4.3);
            p.addEllipse(QPointF(12, 12), 1.0, 1.0);
            break;

        case Glyph::Search:
            // A magnifier.
            p.addEllipse(QPointF(10.5, 10.5), 5.6, 5.6);
            p.moveTo(14.6, 14.6);
            p.lineTo(19.6, 19.6);
            break;

        case Glyph::Settings: {
            // A real gear silhouette -- eight teeth around a ring with a hub.
            // The old radial ticks read as a sun, not settings.
            const QPointF c(12, 12);
            constexpr int teeth = 8;
            const qreal outer = 9.0;
            const qreal valley = 6.7;
            auto at = [&](qreal base, qreal offset_deg, qreal r) {
                const qreal a = base + offset_deg * M_PI / 180.0;
                return QPointF(c.x() + std::cos(a) * r, c.y() + std::sin(a) * r);
            };
            for (int i = 0; i < teeth; ++i) {
                const qreal base = i * (2.0 * M_PI / teeth) - M_PI / 2.0;
                if (i == 0) p.moveTo(at(base, -22.5, valley));
                p.lineTo(at(base, -11, valley));
                p.lineTo(at(base, -7, outer));
                p.lineTo(at(base, 7, outer));
                p.lineTo(at(base, 11, valley));
                p.lineTo(at(base, 22.5, valley));
            }
            p.closeSubpath();
            p.addEllipse(c, 3.1, 3.1);
            break;
        }

        case Glyph::Send:
            p.moveTo(5, 12);
            p.lineTo(19, 12);
            p.moveTo(13.5, 6.5);
            p.lineTo(19, 12);
            p.lineTo(13.5, 17.5);
            break;

        case Glyph::Power:
            p.moveTo(12, 3.5);
            p.lineTo(12, 11);
            p.moveTo(6.9, 6.6);
            p.arcTo(QRectF(4, 5, 16, 16), 137, -274);
            break;

        case Glyph::Clock:
            p.addEllipse(QPointF(12, 12), 8.2, 8.2);
            p.moveTo(12, 7);
            p.lineTo(12, 12);
            p.lineTo(15.5, 14);
            break;

        case Glyph::Battery:
            p.addRoundedRect(QRectF(3, 8, 16, 9), 2.2, 2.2);
            p.moveTo(21, 11);
            p.lineTo(21, 14);
            break;

        case Glyph::Display:
            p.addRoundedRect(QRectF(3, 5, 18, 12), 2.2, 2.2);
            p.moveTo(9, 20.5);
            p.lineTo(15, 20.5);
            break;

        case Glyph::Camera:
            p.addRoundedRect(QRectF(3, 7, 18, 13), 2.4, 2.4);
            p.addEllipse(QPointF(12, 13.5), 3.6, 3.6);
            p.moveTo(9, 7);
            p.lineTo(10.5, 4.2);
            p.lineTo(13.5, 4.2);
            p.lineTo(15, 7);
            break;

        case Glyph::VolumeDown:
            p.moveTo(4, 9.5);
            p.lineTo(8, 9.5);
            p.lineTo(12.5, 5.5);
            p.lineTo(12.5, 18.5);
            p.lineTo(8, 14.5);
            p.lineTo(4, 14.5);
            p.closeSubpath();
            p.moveTo(16.5, 10);
            p.lineTo(20.5, 14);
            break;

        case Glyph::Lock:
            p.addRoundedRect(QRectF(5, 10.5, 14, 10), 2.2, 2.2);
            p.moveTo(8.2, 10.5);
            p.lineTo(8.2, 7.8);
            p.arcTo(QRectF(8.2, 3.4, 7.6, 7.6), 180, -180);
            p.lineTo(15.8, 10.5);
            break;
    }
    return p;
}

QString cache_key(Glyph glyph, const QColor& colour, int size) {
    return QStringLiteral("%1|%2|%3")
        .arg(static_cast<int>(glyph))
        .arg(colour.name(QColor::HexArgb))
        .arg(size);
}

}  // namespace

QPainterPath path(Glyph glyph) { return build(glyph); }

QIcon icon(Glyph glyph, const QColor& colour, int size) {
    static QHash<QString, QIcon> cache;
    const QString key = cache_key(glyph, colour, size);
    if (const auto found = cache.constFind(key); found != cache.constEnd()) return *found;

    // Rendered at 2x and tagged, so it stays sharp on a retina display.
    QPixmap pixmap(size * 2, size * 2);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(pixmap.width() / kGrid, pixmap.height() / kGrid);

        QPen pen(colour, kStroke);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(build(glyph));
    }
    pixmap.setDevicePixelRatio(2.0);

    const QIcon result(pixmap);
    cache.insert(key, result);
    return result;
}

}  // namespace mimi::ui::icons
