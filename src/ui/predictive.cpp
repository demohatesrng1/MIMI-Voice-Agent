#include "ui/predictive.hpp"

#include "ui/theme.hpp"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

#include <array>
#include <functional>
#include <utility>

namespace mimi::ui {
namespace {

QColor mix(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

// A single offered action: a sparkle to mark it as her idea, the guess itself,
// and a chevron to say it is one tap from happening. Lifts on hover, like the
// suggestion chips, but carries more weight -- it is a proposal, not a label.
class PredictiveCard : public QWidget {
public:
    PredictiveCard(const QString& text, QString utterance, QWidget* parent = nullptr)
        : QWidget(parent), text_(text), utterance_(std::move(utterance)) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(44);
        QFont f = font();
        f.setPixelSize(13);
        const int w = QFontMetrics(f).horizontalAdvance(text_) + 62;
        setFixedWidth(w);

        anim_ = new QVariantAnimation(this);
        anim_->setDuration(theme::kMotionMs);
        anim_->setEasingCurve(theme::kMotion);
        connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            hover_ = v.toReal();
            update();
        });
    }

    std::function<void()> onClick;

protected:
    void enterEvent(QEnterEvent*) override { animate(1.0); }
    void leaveEvent(QEvent*) override { animate(0.0); }
    void mouseReleaseEvent(QMouseEvent*) override {
        if (onClick) onClick();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const qreal lift = hover_ * 1.5;
        const QRectF body = QRectF(rect()).adjusted(0.5, 0.5 - lift, -0.5, -0.5 - lift);
        const qreal r = body.height() / 2.0;

        QColor fill(255, 255, 255);
        fill.setAlphaF(0.045 + 0.05 * hover_);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(body, r, r);
        // A hairline that warms toward the accent on hover.
        p.setPen(QPen(mix(QColor(255, 255, 255, 20), theme::kAccent, hover_), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(body, r, r);

        // Sparkle: a small four-point star, the mark of a suggestion.
        const QPointF s(body.left() + 18, body.center().y());
        QPainterPath star;
        star.moveTo(s.x(), s.y() - 5);
        star.quadTo(s.x() + 1, s.y() - 1, s.x() + 5, s.y());
        star.quadTo(s.x() + 1, s.y() + 1, s.x(), s.y() + 5);
        star.quadTo(s.x() - 1, s.y() + 1, s.x() - 5, s.y());
        star.quadTo(s.x() - 1, s.y() - 1, s.x(), s.y() - 5);
        p.setBrush(mix(theme::kAccentDeep, theme::kAccentSoft, hover_));
        p.setPen(Qt::NoPen);
        p.drawPath(star);

        QFont f = font();
        f.setPixelSize(13);
        p.setFont(f);
        p.setPen(mix(theme::kDim, theme::kInk, hover_));
        p.drawText(body.adjusted(32, 0, -22, -0.0), Qt::AlignVCenter | Qt::AlignLeft, text_);

        // Trailing chevron.
        const qreal cx = body.right() - 14;
        const qreal cy = body.center().y();
        QPen chev(mix(theme::kFaint, theme::kAccentSoft, hover_), 1.6);
        chev.setCapStyle(Qt::RoundCap);
        chev.setJoinStyle(Qt::RoundJoin);
        p.setPen(chev);
        p.setBrush(Qt::NoBrush);
        QPainterPath arrow;
        arrow.moveTo(cx - 2, cy - 4);
        arrow.lineTo(cx + 2, cy);
        arrow.lineTo(cx - 2, cy + 4);
        p.drawPath(arrow);
    }

private:
    void animate(qreal to) {
        anim_->stop();
        anim_->setStartValue(hover_);
        anim_->setEndValue(to);
        anim_->start();
    }

    QString text_;
    QString utterance_;
    qreal hover_ = 0.0;
    QVariantAnimation* anim_ = nullptr;
};

struct Prediction {
    const char* label;
    const char* utterance;
};

// Things she can actually do, phrased the way you would say them.
//
// These were three invented office tasks ("Continue yesterday's report") that
// routed to nothing -- a chip that does not work is worse than no chip, because
// it teaches you not to trust the surface. Every entry here runs a real path
// through the router.
constexpr std::array<Prediction, 3> kPredictions{{
    {"Take a note", "メモして"},
    {"Read my notes", "メモを読んで"},
    {"What's on screen?", "画面に何がある"},
}};

}  // namespace

PredictiveActions::PredictiveActions(QWidget* parent) : QWidget(parent) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);
    row->addStretch(1);
    for (const Prediction& p : kPredictions) {
        auto* card = new PredictiveCard(QString::fromUtf8(p.label),
                                        QString::fromUtf8(p.utterance));
        const QString utterance = QString::fromUtf8(p.utterance);
        card->onClick = [this, utterance] { Q_EMIT commandRequested(utterance); };
        row->addWidget(card);
    }
    row->addStretch(1);
}

}  // namespace mimi::ui
