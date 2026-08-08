#include "ui/face_picker.hpp"

#include "ui/faces.hpp"
#include "ui/theme.hpp"

#include <QMouseEvent>
#include <QPainter>

namespace mimi::ui {
namespace {

constexpr int kTile = 84;   // portrait diameter
constexpr int kGap = 16;
constexpr int kLabelH = 22;

}  // namespace

FacePicker::FacePicker(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    selected_ = faces::currentId();
}

QSize FacePicker::sizeHint() const {
    const int count = faces::all().size();
    return {count * kTile + (count - 1) * kGap, kTile + kLabelH};
}

QRect FacePicker::tileRect(int index) const {
    return {index * (kTile + kGap), 0, kTile, kTile};
}

void FacePicker::setSelected(const QString& id) {
    for (const auto& face : faces::all()) {
        if (face.id != id) continue;
        selected_ = id;
        update();
        return;
    }
}

void FacePicker::mousePressEvent(QMouseEvent* event) {
    const auto& list = faces::all();
    for (int i = 0; i < list.size(); ++i) {
        if (!tileRect(i).contains(event->pos())) continue;
        if (list[i].id == selected_) return;
        selected_ = list[i].id;
        update();
        Q_EMIT chosen(selected_);
        return;
    }
}

void FacePicker::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& list = faces::all();
    for (int i = 0; i < list.size(); ++i) {
        const QRect rect = tileRect(i);
        const bool active = list[i].id == selected_;

        // The unchosen ones sit back rather than being greyed out: they are all
        // valid choices, and a dimmed option reads as unavailable.
        painter.setOpacity(active ? 1.0 : 0.62);
        painter.drawPixmap(rect, faces::circular(list[i].id, kTile * 2));
        painter.setOpacity(1.0);

        if (active) {
            // A ring in the accent, outside the portrait so it never crops her.
            QPen pen(theme::kAccent, 2.5);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(rect.adjusted(-3, -3, 3, 3));
        }

        painter.setPen(active ? theme::kInk : theme::kFaint);
        QFont label = painter.font();
        label.setPointSizeF(10.5);
        label.setWeight(active ? QFont::DemiBold : QFont::Normal);
        painter.setFont(label);
        painter.drawText(QRect(rect.left(), rect.bottom() + 4, rect.width(), kLabelH),
                         Qt::AlignHCenter | Qt::AlignTop, list[i].label);
    }
}

}  // namespace mimi::ui
