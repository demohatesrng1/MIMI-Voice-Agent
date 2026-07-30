#include "ui/smart_voice.hpp"

#include "ui/theme.hpp"

#include <QFontMetrics>
#include <QPainter>

#include <array>

namespace mimi::ui {

SmartVoiceBar::SmartVoiceBar(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedHeight(26);
}

void SmartVoiceBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    static const std::array<const char*, 3> caps{{"Interrupt", "Pause", "Resume"}};
    QFont f = font();
    f.setPixelSize(11);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    const QFontMetrics fm(f);

    // Centre the row of capability tokens.
    int total = 0;
    for (const char* c : caps) total += fm.horizontalAdvance(QString::fromUtf8(c)) + 26;
    int x = (width() - total) / 2;
    const int cy = height() / 2;

    for (const char* c : caps) {
        p.setBrush(theme::kAccent);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(x + 4, cy), 2.4, 2.4);
        const QString t = QString::fromUtf8(c);
        p.setPen(theme::kDim);
        p.drawText(QRect(x + 12, 0, fm.horizontalAdvance(t) + 4, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, t);
        x += fm.horizontalAdvance(t) + 26;
    }
}

}  // namespace mimi::ui
