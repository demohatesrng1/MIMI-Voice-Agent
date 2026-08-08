#include "ui/faces.hpp"

#include "brain/account.hpp"

#include <QHash>
#include <QPainter>
#include <QPainterPath>

namespace mimi::ui::faces {
namespace {

// A cache keyed by face and size. The orb repaints at ~12 fps and the puck
// across every Space; masking a 640px source to a circle each time would be
// pure waste.
QHash<QString, QPixmap>& cache() {
    static QHash<QString, QPixmap> instance;
    return instance;
}

QPixmap load(const QString& resource, int size) {
    QPixmap source(resource);
    if (source.isNull()) return {};
    return source.scaled(size, size, Qt::KeepAspectRatioByExpanding,
                         Qt::SmoothTransformation);
}

}  // namespace

const QVector<Face>& all() {
    // The labels name the expression rather than the number, because "the one
    // where she is waving" is how a person actually picks.
    static const QVector<Face> faces{
        {QStringLiteral("face1"), QStringLiteral("Waving"),   QStringLiteral(":/faces/face1.png")},
        {QStringLiteral("face2"), QStringLiteral("Laughing"), QStringLiteral(":/faces/face2.png")},
        {QStringLiteral("face3"), QStringLiteral("Delighted"),QStringLiteral(":/faces/face3.png")},
        {QStringLiteral("face4"), QStringLiteral("Cheering"), QStringLiteral(":/faces/face4.png")},
        {QStringLiteral("face5"), QStringLiteral("Playful"),  QStringLiteral(":/faces/face5.png")},
        {QStringLiteral("face6"), QStringLiteral("Sunny"),    QStringLiteral(":/faces/face6.png")},
    };
    return faces;
}

QString currentId() {
    const QString saved = QString::fromStdString(brain::Accounts().load().face);
    for (const Face& face : all()) {
        if (face.id == saved) return saved;
    }
    // Nothing chosen yet, or artwork that no longer ships. Either way the first
    // face is a working answer, and a missing portrait must never be a hole.
    return all().front().id;
}

bool choose(const QString& id) {
    for (const Face& face : all()) {
        if (face.id != id) continue;
        cache().clear();  // the current-face entries are now stale
        return brain::Accounts().set_face(id.toStdString());
    }
    return false;
}

QPixmap square(const QString& id, int size) {
    for (const Face& face : all()) {
        if (face.id == id) return load(face.resource, size);
    }
    return {};
}

QPixmap circular(const QString& id, int diameter) {
    if (diameter <= 0) return {};
    const QString key = id + QLatin1Char('@') + QString::number(diameter);
    if (const auto hit = cache().constFind(key); hit != cache().constEnd()) return *hit;

    const QPixmap source = square(id, diameter);
    if (source.isNull()) return {};

    QPixmap masked(diameter, diameter);
    masked.fill(Qt::transparent);
    QPainter painter(&masked);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath circle;
    circle.addEllipse(0, 0, diameter, diameter);
    painter.setClipPath(circle);
    // Centred: KeepAspectRatioByExpanding can overshoot on one axis.
    painter.drawPixmap((diameter - source.width()) / 2,
                       (diameter - source.height()) / 2, source);
    painter.end();

    cache().insert(key, masked);
    return masked;
}

QPixmap current(int diameter) { return circular(currentId(), diameter); }

}  // namespace mimi::ui::faces
