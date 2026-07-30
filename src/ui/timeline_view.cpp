#include "ui/timeline_view.hpp"

#include "ui/theme.hpp"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

namespace mimi::ui {
namespace {

constexpr int kTopPad = 22;
constexpr int kHeaderH = 46;
constexpr int kRowH = 94;
constexpr int kBottomPad = 28;
constexpr int kDotX = 108;    // the spine
constexpr int kCardX = 140;   // left edge of the memory cards
constexpr int kCardH = 74;
constexpr int kRightPad = 28;

struct KindStyle {
    const char* name;
    QColor accent;
};

KindStyle styleFor(Memory::Kind kind) {
    switch (kind) {
        case Memory::Kind::Meeting: return {"MEETING", theme::kAccent};
        case Memory::Kind::Report:  return {"REPORT",  theme::kAccentSoft};
        case Memory::Kind::Chat:    return {"CHAT",    theme::kAccentGlow};
        case Memory::Kind::Note:    return {"NOTE",    theme::kDim};
        case Memory::Kind::Action:  return {"ACTION",  theme::kAccent};
        case Memory::Kind::File:    return {"FILE",    theme::kAccentSoft};
    }
    return {"NOTE", theme::kDim};
}

}  // namespace

// ---------------------------------------------------------------- TimelineStrip

TimelineStrip::TimelineStrip(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("timelineStrip"));
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);
}

int TimelineStrip::contentHeight() const {
    int y = kTopPad;
    QString prevDay;
    for (const Memory& m : memories_) {
        if (m.day != prevDay) {
            y += kHeaderH;
            prevDay = m.day;
        }
        y += kRowH;
    }
    return y + kBottomPad;
}

void TimelineStrip::relayout() {
    centres_.clear();
    int y = kTopPad;
    QString prevDay;
    for (const Memory& m : memories_) {
        if (m.day != prevDay) {
            y += kHeaderH;
            prevDay = m.day;
        }
        centres_.push_back(y + kRowH / 2);
        y += kRowH;
    }
    setMinimumHeight(contentHeight());
    update();
}

void TimelineStrip::setMemories(QVector<Memory> memories) {
    memories_ = std::move(memories);
    relayout();
}

void TimelineStrip::prepend(const Memory& memory) {
    memories_.prepend(memory);
    relayout();
}

void TimelineStrip::mouseMoveEvent(QMouseEvent* event) {
    int found = -1;
    for (int i = 0; i < centres_.size(); ++i) {
        if (qAbs(centres_[i] - event->pos().y()) <= kRowH / 2 &&
            event->pos().x() >= kCardX - 30) {
            found = i;
            break;
        }
    }
    if (found != hover_) {
        hover_ = found;
        setCursor(found >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void TimelineStrip::leaveEvent(QEvent*) {
    if (hover_ != -1) {
        hover_ = -1;
        update();
    }
}

void TimelineStrip::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (memories_.isEmpty()) return;

    // The spine, threading every memory from the first to the last. A vertical
    // gradient so it reads as flowing down through time rather than as a border.
    if (centres_.size() >= 1) {
        const int top = centres_.first();
        const int bottom = centres_.last();
        QLinearGradient g(0, top, 0, bottom);
        QColor c = theme::kAccentDeep;
        c.setAlphaF(0.55);
        g.setColorAt(0.0, c);
        c.setAlphaF(0.22);
        g.setColorAt(1.0, c);
        painter.setPen(QPen(QBrush(g), 2.0));
        painter.drawLine(kDotX, top, kDotX, bottom);
    }

    int y = kTopPad;
    QString prevDay;
    for (int i = 0; i < memories_.size(); ++i) {
        const Memory& m = memories_[i];
        if (m.day != prevDay) {
            // Day header: a tracked-out label with a hairline reaching right.
            QFont head = font();
            head.setPixelSize(10);
            head.setWeight(QFont::DemiBold);
            head.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
            painter.setFont(head);
            painter.setPen(theme::kAccent);
            painter.drawText(QRect(24, y, 200, kHeaderH), Qt::AlignVCenter | Qt::AlignLeft,
                             m.day.toUpper());
            QColor rule = theme::kFaint;
            rule.setAlphaF(0.25);
            painter.setPen(QPen(rule, 1.0));
            painter.drawLine(120, y + kHeaderH / 2, width() - kRightPad, y + kHeaderH / 2);
            y += kHeaderH;
            prevDay = m.day;
        }

        const int cy = y + kRowH / 2;
        const bool hot = i == hover_;
        const KindStyle style = styleFor(m.kind);

        // Time, in the gutter left of the spine.
        QFont timeFont = font();
        timeFont.setPixelSize(11);
        painter.setFont(timeFont);
        painter.setPen(theme::kFaint);
        painter.drawText(QRect(0, cy - 10, kDotX - 22, 20), Qt::AlignVCenter | Qt::AlignRight,
                         m.time);

        // The node on the spine.
        painter.setPen(Qt::NoPen);
        QColor halo = style.accent;
        halo.setAlphaF(hot ? 0.28 : 0.16);
        painter.setBrush(halo);
        painter.drawEllipse(QPointF(kDotX, cy), hot ? 11 : 9, hot ? 11 : 9);
        painter.setBrush(style.accent);
        painter.drawEllipse(QPointF(kDotX, cy), 4.2, 4.2);

        // Connector from the spine to the card.
        QColor link = style.accent;
        link.setAlphaF(0.35);
        painter.setPen(QPen(link, 1.2));
        painter.drawLine(kDotX + 6, cy, kCardX, cy);

        // The card.
        const QRectF card(kCardX, cy - kCardH / 2.0, width() - kRightPad - kCardX, kCardH);
        QColor glass = theme::kLayer2;
        glass.setAlpha(hot ? 250 : 232);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hot ? glass.lighter(112) : glass);
        painter.drawRoundedRect(card, 12, 12);
        if (hot) {
            QColor rim = style.accent;
            rim.setAlphaF(0.7);
            painter.setPen(QPen(rim, 1.4));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(card.adjusted(0.7, 0.7, -0.7, -0.7), 11, 11);
        } else {
            painter.setPen(QPen(QColor(255, 255, 255, 16), 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 11.5, 11.5);
        }

        // Kind tag, title, detail inside the card.
        painter.setBrush(style.accent);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(card.left() + 16, card.top() + 18), 3.0, 3.0);

        QFont tag = font();
        tag.setPixelSize(9);
        tag.setWeight(QFont::DemiBold);
        tag.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
        painter.setFont(tag);
        QColor tagColour = style.accent;
        tagColour.setAlphaF(0.9);
        painter.setPen(tagColour);
        painter.drawText(QRectF(card.left() + 26, card.top() + 10, card.width() - 40, 16),
                         Qt::AlignVCenter | Qt::AlignLeft, QString::fromUtf8(style.name));

        QFont titleFont = font();
        titleFont.setPixelSize(14);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.setPen(theme::kInk);
        painter.drawText(QRectF(card.left() + 16, card.top() + 28, card.width() - 32, 22),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(titleFont).elidedText(
                             m.title, Qt::ElideRight, static_cast<int>(card.width() - 32)));

        QFont detailFont = font();
        detailFont.setPixelSize(11);
        painter.setFont(detailFont);
        painter.setPen(theme::kDim);
        painter.drawText(QRectF(card.left() + 16, card.top() + 50, card.width() - 32, 18),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(detailFont).elidedText(
                             m.detail, Qt::ElideRight, static_cast<int>(card.width() - 32)));

        y += kRowH;
    }
}

// ----------------------------------------------------------------- TimelineView

TimelineView::TimelineView(QWidget* parent) : QScrollArea(parent) {
    setObjectName(QStringLiteral("timeline"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAttribute(Qt::WA_TranslucentBackground);
    viewport()->setAttribute(Qt::WA_TranslucentBackground);
    viewport()->setStyleSheet(QStringLiteral("background: transparent;"));

    strip_ = new TimelineStrip;
    setWidget(strip_);

    // Seeded so the timeline tells a story on first open: yesterday's meeting
    // becoming a report, a proposal, feedback, and a final version -- the exact
    // chain the product is built to make legible. Today fills in live.
    strip_->setMemories({
        {Memory::Kind::Report, QStringLiteral("09:02"), QStringLiteral("Morning digest"),
         QStringLiteral("Three follow-ups from yesterday are waiting for you."),
         QStringLiteral("Today")},
        {Memory::Kind::File, QStringLiteral("17:20"), QStringLiteral("Final version"),
         QStringLiteral("Exported the proposal and shared it with the team."),
         QStringLiteral("Yesterday")},
        {Memory::Kind::Action, QStringLiteral("16:40"), QStringLiteral("Client feedback"),
         QStringLiteral("Two changes requested, both on the pricing section."),
         QStringLiteral("Yesterday")},
        {Memory::Kind::Note, QStringLiteral("14:05"), QStringLiteral("Edited proposal"),
         QStringLiteral("Tightened the positioning and cut the third slide."),
         QStringLiteral("Yesterday")},
        {Memory::Kind::Report, QStringLiteral("10:30"), QStringLiteral("Generated summary"),
         QStringLiteral("Auto-drafted from the kickoff transcript."),
         QStringLiteral("Yesterday")},
        {Memory::Kind::Meeting, QStringLiteral("09:15"), QStringLiteral("Client kickoff"),
         QStringLiteral("Scope, timeline, and the three deliverables."),
         QStringLiteral("Yesterday")},
    });
}

void TimelineView::remember(const QString& said, const QString& replied) {
    Memory memory;
    memory.kind = Memory::Kind::Chat;
    memory.time = QStringLiteral("Just now");
    memory.title = said.trimmed().isEmpty() ? QStringLiteral("Mimi") : said.trimmed();
    memory.detail = replied.trimmed();
    memory.day = QStringLiteral("Today");
    strip_->prepend(memory);
    verticalScrollBar()->setValue(0);  // the newest memory, in view
}

}  // namespace mimi::ui
