#include "ui/context_ribbon.hpp"

#include "ui/theme.hpp"

#include <QFontMetrics>
#include <QPainter>

namespace mimi::ui {
namespace {

constexpr int kHeight = 24;
constexpr int kPadX = 24;

}  // namespace

ContextRibbon::ContextRibbon(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedHeight(kHeight);
    // No placeholder content. This used to open on an invented task
    // ("Marketing proposal") with invented counts beside it, which nothing ever
    // replaced -- the ribbon stated four facts about your work that were false
    // on every machine it ran on. It now stays blank until MainWindow feeds it
    // something true.
}

QSize ContextRibbon::sizeHint() const { return {600, kHeight}; }

void ContextRibbon::setTask(const QString& task) {
    task_ = task;
    update();
}

void ContextRibbon::setCompact(bool compact) {
    if (compact == compact_) return;
    compact_ = compact;
    update();
}

void ContextRibbon::setMetric(const QString& label, const QString& value) {
    for (auto& metric : metrics_) {
        if (metric.first.compare(label, Qt::CaseInsensitive) == 0) {
            metric.second = value;
            update();
            return;
        }
    }
    metrics_.append({label.toUpper(), value});
    update();
}

void ContextRibbon::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // No band -- just a hairline at the foot, so it belongs to the one surface
    // rather than stacking a toolbar on top of it.
    painter.setPen(QPen(QColor(255, 255, 255, 16), 1.0));
    painter.drawLine(0, 0, width(), 0);

    QFont caps = font();
    caps.setPixelSize(12);
    caps.setWeight(QFont::DemiBold);
    caps.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    QFont value = font();
    value.setPixelSize(14);
    value.setWeight(QFont::DemiBold);

    // Left: what she is looking at. Blank until something real is set, rather
    // than a placeholder that reads as fact.
    if (!task_.isEmpty()) {
        painter.setFont(caps);
        painter.setPen(theme::kFaint);
        const int tagW =
            QFontMetrics(caps).horizontalAdvance(QStringLiteral("IN FRONT")) + 10;
        painter.drawText(QRect(kPadX, 0, tagW, height()), Qt::AlignVCenter | Qt::AlignLeft,
                         QStringLiteral("IN FRONT"));
        painter.setFont(value);
        painter.setPen(theme::kInk);
        painter.drawText(QRect(kPadX + tagW, 0, width() / 2, height()),
                         Qt::AlignVCenter | Qt::AlignLeft, task_);
    }

    // Simple mode stops here -- just the task, nothing to read on the right.
    if (compact_) return;

    // Right: the connected-context metrics, laid out from the right edge in.
    const QFontMetrics fmCaps(caps);
    const QFontMetrics fmVal(value);
    int x = width() - kPadX;
    for (int i = metrics_.size() - 1; i >= 0; --i) {
        const QString& label = metrics_[i].first;
        const QString& val = metrics_[i].second;
        const int valW = fmVal.horizontalAdvance(val);
        const int labW = fmCaps.horizontalAdvance(label);
        const int cellW = labW + 8 + valW;
        x -= cellW;

        painter.setFont(caps);
        painter.setPen(theme::kFaint);
        painter.drawText(QRect(x, 0, labW, height()), Qt::AlignVCenter | Qt::AlignLeft, label);
        painter.setFont(value);
        painter.setPen(val == QStringLiteral("High") ? theme::kAccentSoft : theme::kInk);
        painter.drawText(QRect(x + labW + 8, 0, valW, height()),
                         Qt::AlignVCenter | Qt::AlignLeft, val);

        x -= 22;  // gap before the divider
        if (i > 0) {
            QColor div = theme::kFaint;
            div.setAlphaF(0.30);
            painter.setPen(QPen(div, 1.0));
            painter.drawLine(x + 11, height() / 2 - 6, x + 11, height() / 2 + 6);
        }
    }
}

}  // namespace mimi::ui
