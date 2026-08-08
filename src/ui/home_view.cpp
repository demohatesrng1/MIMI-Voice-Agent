#include "ui/home_view.hpp"

#include "ui/controls.hpp"
#include "ui/live_thinking.hpp"
#ifdef MIMI_HAS_AVATAR
#include "ui/avatar_view.hpp"
#endif
#include "brain/account.hpp"
#include "ui/command_bar.hpp"
#include "ui/neural_sidebar.hpp"
#include "ui/lumin_orb.hpp"
#include "ui/theme.hpp"
#include "ui/voice_orb.hpp"
#include "ui/workspace_dock.hpp"

#include <QAbstractButton>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QStyle>
#include <QVBoxLayout>

namespace mimi::ui {

// A glass card: a low-alpha lift with a brighter hairline, and a deep shadow so
// it floats over the stage rather than sitting in it. The primary one borrows
// the orb's light, which is what carries the eye from her to the thing to press.
class StageCard : public QAbstractButton {
public:
    StageCard(QString name, QString note, bool primary, QWidget* parent = nullptr)
        : QAbstractButton(parent), name_(std::move(name)), note_(std::move(note)),
          primary_(primary) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setMinimumHeight(104);
        setMinimumWidth(200);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setAccent(const QColor& accent) { accent_ = accent; update(); }
    void setHint(const QString& hint) { hint_ = hint; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool hot = underMouse() || hasFocus();

        QColor fill = primary_ ? QColor(accent_.red(), accent_.green(), accent_.blue(),
                                        hot ? 46 : 30)
                               : QColor(255, 255, 255, hot ? 19 : 11);
        QColor edge = primary_ ? QColor(accent_.red(), accent_.green(), accent_.blue(),
                                        hot ? 150 : 92)
                               : QColor(255, 255, 255, hot ? 46 : 23);

        painter.setPen(QPen(edge, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(box, 18, 18);

        // Name, then the note under it.
        painter.setPen(theme::kInk);
        QFont name = font();
        name.setPointSizeF(15.5);
        name.setWeight(QFont::Medium);
        painter.setFont(name);
        const int left = 20;
        painter.drawText(QRect(left, height() - 54, width() - left * 2, 22),
                         Qt::AlignLeft | Qt::AlignVCenter, name_);

        painter.setPen(theme::kFaint);
        QFont note = font();
        note.setPointSizeF(12.0);
        painter.setFont(note);
        // Elided, not clipped: a sentence that runs off the edge of a card reads
        // as a rendering fault rather than as a truncation.
        const int noteWidth = width() - left * 2;
        const QString shown =
            QFontMetrics(note).elidedText(note_, Qt::ElideRight, noteWidth);
        painter.drawText(QRect(left, height() - 32, noteWidth, 20),
                         Qt::AlignLeft | Qt::AlignVCenter, shown);

        // The hint pill, top right: how else to reach this.
        if (!hint_.isEmpty()) {
            QFont pill = font();
            pill.setPointSizeF(9.5);
            pill.setWeight(QFont::Medium);
            painter.setFont(pill);
            const QRect text = QFontMetrics(pill).boundingRect(hint_);
            const QRectF chip(width() - text.width() - 34, 16, text.width() + 16, 20);
            painter.setPen(QPen(QColor(255, 255, 255, 30), 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(chip, 6, 6);
            painter.setPen(theme::kFaint);
            painter.drawText(chip, Qt::AlignCenter, hint_);
        }

        // A dot in the orb's light, so each card is anchored by the same colour.
        painter.setPen(Qt::NoPen);
        painter.setBrush(primary_ ? accent_ : QColor(accent_.red(), accent_.green(),
                                                     accent_.blue(), 150));
        painter.drawEllipse(QPointF(left + 4, 26), 4.0, 4.0);
    }

private:
    QString name_;
    QString note_;
    QString hint_;
    QColor accent_{theme::kAccent};
    bool primary_ = false;
};


HomeView::HomeView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("home"));

    // A command centre, split. Left is her -- and nothing but her, plus what
    // has been said and the way to say more. Right is the state of the machine,
    // floating clear of it on glass. The two never share a column, so she can
    // never be crowded out of her own screen.
    auto* split = new QHBoxLayout(this);
    split->setContentsMargins(30, 16, 26, 20);
    split->setSpacing(26);

    // ---------------------------------------------------------------- left
    auto* left = new QVBoxLayout;
    left->setSpacing(0);
    left->addStretch(3);

    // The companion, and she is the real model -- not a generated sphere.
    // The orb only ever exists as the fallback for a machine with no .vrm.
#ifdef MIMI_HAS_AVATAR
    if (AvatarView::available()) {
        avatar_ = new AvatarView(this);
        avatar_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        avatar_->setMinimumSize(360, 420);
        left->addWidget(avatar_, 6);
        connect(avatar_, &AvatarView::failed, this, [this] {
            // No WebGL, or a model that will not parse. Something has to stand
            // in her place rather than leaving a hole in the screen.
            if (avatar_ == nullptr) return;
            avatar_->hide();
            avatar_->deleteLater();
            avatar_ = nullptr;
            if (lumin_ != nullptr) lumin_->show();
        });
    }
#endif

    lumin_ = new LuminOrb(this);
    lumin_->setMinimumSize(320, 320);
    lumin_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(lumin_, &LuminOrb::held, this, &HomeView::voiceRequested);
    connect(lumin_, &LuminOrb::lightChanged, this, &HomeView::applyLight);
    left->addWidget(lumin_, 6, Qt::AlignHCenter);
    if (avatar_ != nullptr) lumin_->hide();

    left->addSpacing(6);

    // The exchange, as floating type over the void rather than a scrollable
    // list. You are looking at the conversation you are in, not its archive.
    greetingLabel_ = new QLabel(this);
    greetingLabel_->setObjectName(QStringLiteral("greeting"));
    greetingLabel_->setAlignment(Qt::AlignCenter);
    greetingLabel_->setWordWrap(true);
    left->addWidget(greetingLabel_);

    marqueeLabel_ = new QLabel(this);
    marqueeLabel_->setObjectName(QStringLiteral("marquee"));
    marqueeLabel_->setAlignment(Qt::AlignCenter);
    left->addWidget(marqueeLabel_);

    static const QStringList kStates{
        QStringLiteral("Here to listen."),
        QStringLiteral("Brainstorming with you."),
        QStringLiteral("Processing your day."),
    };
    marqueeLabel_->setText(kStates.first());
    marqueeTimer_ = new QTimer(this);
    marqueeTimer_->setInterval(3600);
    connect(marqueeTimer_, &QTimer::timeout, this, [this] {
        marqueeIndex_ = (marqueeIndex_ + 1) % kStates.size();
        marqueeLabel_->setText(kStates.at(marqueeIndex_));
    });
    marqueeTimer_->start();

    saidLabel_ = new QLabel(this);
    saidLabel_->setObjectName(QStringLiteral("bubbleSaid"));
    saidLabel_->setAlignment(Qt::AlignCenter);
    saidLabel_->setWordWrap(true);
    saidLabel_->setVisible(false);
    left->addWidget(saidLabel_, 0, Qt::AlignHCenter);

    left->addSpacing(8);

    replyLabel_ = new QLabel(this);
    replyLabel_->setObjectName(QStringLiteral("bubbleReply"));
    replyLabel_->setAlignment(Qt::AlignCenter);
    replyLabel_->setWordWrap(true);
    replyLabel_->setVisible(false);
    left->addWidget(replyLabel_, 0, Qt::AlignHCenter);

    live_ = new LiveThinking(this);
    live_->setVisible(false);
    left->addWidget(live_, 0, Qt::AlignHCenter);

    // A wrapped QLabel only reports the height its text actually needs if the
    // policy says so; without this the layout hands it a single line's worth
    // and the bubbles clip their own contents.
    for (QLabel* bubble : {greetingLabel_, saidLabel_, replyLabel_}) {
        // Fixed, not maximum: the height is computed from heightForWidth(), and
        // a centred label that renders narrower than the width we measured wraps
        // onto more lines than we allowed for and clips the first one.
        bubble->setFixedWidth(480);
        QSizePolicy policy = bubble->sizePolicy();
        policy.setHeightForWidth(true);
        policy.setVerticalPolicy(QSizePolicy::Minimum);
        bubble->setSizePolicy(policy);
    }
    // Real margins, not stylesheet padding: QSS padding on a QLabel is painted
    // by the style but never reaches heightForWidth(), so the bubble sizes
    // itself for the text and then clips it.
    saidLabel_->setContentsMargins(18, 10, 18, 10);
    replyLabel_->setContentsMargins(24, 18, 24, 18);

    left->addStretch(2);

    // The way to say something, at the foot of her half.
    composer_ = new CommandBar(this);
    composer_->setMaximumWidth(560);
    connect(composer_, &CommandBar::submitted, this, &HomeView::commandRequested);
    connect(composer_, &CommandBar::micClicked, this, &HomeView::voiceRequested);
    left->addWidget(composer_, 0, Qt::AlignHCenter);

    split->addLayout(left, 62);

    // --------------------------------------------------------------- right
    sidebar_ = new NeuralSidebar(this);
    sidebar_->setMinimumWidth(300);
    split->addWidget(sidebar_, 34);

    // Everything the old home screen carried, kept alive but off this surface.
    dock_ = new WorkspaceDock(this);
    dock_->hide();
    connect(dock_, &WorkspaceDock::commandRequested, this, &HomeView::commandRequested);
    orb_ = new VoiceOrb(this);
    orb_->hide();
    stateLabel_ = new QLabel(this);
    stateLabel_->hide();
    cardVoice_ = nullptr;
    cardReflect_ = nullptr;
    cardBrainstorm_ = nullptr;

    applyLight();
}

void HomeView::applyLight() {
    if (lumin_ == nullptr || greetingLabel_ == nullptr) return;
    const brain::Account account = brain::Accounts().load();
    const QString who = QString::fromStdString(
        account.preferred.empty() ? account.username : account.preferred);
    const QString hello = who.isEmpty() ? lumin_->greetingWord()
                                        : QStringLiteral("%1, %2").arg(
                                              lumin_->greetingWord(), who);
    greetingLabel_->setText(
        QStringLiteral("%1. <span style=\"color:%2\">I'm ready.</span>")
            .arg(hello, theme::kFaint.name()));
}

void HomeView::refreshSidebar() {
    if (sidebar_ != nullptr) sidebar_->refresh();
}

void HomeView::layoutStage() {}

void HomeView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
}

void HomeView::setPresence(Presence presence) {
    if (lumin_ != nullptr) lumin_->setPresence(presence);
    orb_->setPresence(presence);
#ifdef MIMI_HAS_AVATAR
    if (avatar_ != nullptr) avatar_->setPresence(presence);
#endif
    stateLabel_->setText(presence_headline(presence));
    stateLabel_->setProperty("mode", static_cast<int>(presence));
    // Re-polish so the stylesheet can pick up the changed property.
    stateLabel_->style()->unpolish(stateLabel_);
    stateLabel_->style()->polish(stateLabel_);
    // Anticipation belongs to the quiet moments: offer next steps only when she
    // is idle and watching, never mid-exchange.
}

void HomeView::setLevel(float rms) {
    if (lumin_ != nullptr) lumin_->setLevel(rms);
    orb_->setLevel(rms);
#ifdef MIMI_HAS_AVATAR
    if (avatar_ != nullptr) avatar_->setLevel(rms);
#endif
}

void HomeView::setExchange(const QString& said, const QString& replied) {
    saidLabel_->setText(said.isEmpty() ? QString() : QStringLiteral("「%1」").arg(said));
    saidLabel_->setVisible(!said.isEmpty());
    if (!replied.isEmpty()) {
        live_->finish();
        live_->setVisible(false);
        replyLabel_->setVisible(true);
        replyLabel_->setText(replied);
    }
    // Size each bubble to the text it now holds. heightForWidth() is the only
    // thing that knows how many lines the wrapped string takes, and Japanese
    // line spacing is taller than the Latin metrics the layout assumed.
    for (QLabel* bubble : {saidLabel_, replyLabel_}) {
        if (bubble->text().isEmpty()) continue;
        const int wide = bubble->width() > 0 ? bubble->width() : 480;
        const QMargins m = bubble->contentsMargins();
        bubble->setMinimumHeight(bubble->heightForWidth(wide) + m.top() + m.bottom() + 8);
    }

    // The greeting steps aside for an answer rather than being pushed down the
    // page; the marquee goes with it, since it is only ever ambient.
    const bool talking = !said.isEmpty() || !replied.isEmpty();
    greetingLabel_->setVisible(!talking);
    marqueeLabel_->setVisible(!talking);
    if (talking) marqueeTimer_->stop(); else marqueeTimer_->start();
}

void HomeView::setThinking() {
    // Swap the answer slot for the live-thinking pipeline.
    replyLabel_->setVisible(false);
    greetingLabel_->setVisible(false);
    marqueeLabel_->setVisible(false);
    marqueeTimer_->stop();
    live_->setVisible(true);
    live_->start();
}

}  // namespace mimi::ui
