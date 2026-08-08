#include "ui/home_view.hpp"

#include "ui/controls.hpp"
#include "ui/live_thinking.hpp"
#ifdef MIMI_HAS_AVATAR
#include "ui/avatar_view.hpp"
#endif
#include "ui/voice_orb.hpp"
#include "ui/workspace_dock.hpp"

#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>

namespace mimi::ui {

HomeView::HomeView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("home"));

    // Not a layout. She stands full height down the right of the window and
    // the conversation floats over the stage next to her, so the two overlap
    // by design -- which is exactly what a box layout exists to prevent.
    // Everything here is placed by hand in layoutStage().

#ifdef MIMI_HAS_AVATAR
    // Her, full length. The VRM when there is one to show, and the 2D orb when
    // there is not -- the avatar needs a licensed model file that is not in the
    // repo, so a fresh checkout has to come up with something rather than a
    // hole in the page.
    if (AvatarView::available()) {
        avatar_ = new AvatarView(this);
        connect(avatar_, &AvatarView::failed, this, [this] {
            if (avatar_ == nullptr) return;
            avatar_->hide();
            avatar_->deleteLater();
            avatar_ = nullptr;
            if (orb_ != nullptr) orb_->show();
            layoutStage();
        });
    }
#endif

    orb_ = new VoiceOrb(this);
    orb_->setFixedSize(200, 200);
#ifdef MIMI_HAS_AVATAR
    if (avatar_ != nullptr) orb_->hide();
#endif

    // The caption block: what you said, small and quiet above; her answer
    // below, large. No transcript to scroll -- you glance at the exchange in
    // progress, and the timeline page is where history lives.
    stateLabel_ = new QLabel(QStringLiteral("STARTING"), this);
    stateLabel_->setObjectName(QStringLiteral("heroState"));

    saidLabel_ = new QLabel(this);
    saidLabel_->setObjectName(QStringLiteral("heroSaid"));
    saidLabel_->setWordWrap(true);
    saidLabel_->setVisible(false);

    replyLabel_ = new QLabel(QStringLiteral("Ask anything — or just start talking."), this);
    replyLabel_->setObjectName(QStringLiteral("heroReply"));
    replyLabel_->setWordWrap(true);

    live_ = new LiveThinking(this);
    live_->setVisible(false);

    // The captions sit on top of her, so without this every drag that began on
    // a word would be eaten by a label instead of turning the camera.
    for (QWidget* caption : {static_cast<QWidget*>(stateLabel_),
                             static_cast<QWidget*>(saidLabel_),
                             static_cast<QWidget*>(replyLabel_),
                             static_cast<QWidget*>(live_)}) {
        caption->setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    // The tools still follow what you are doing, but they sit at the foot of
    // the caption column now rather than across the whole page: a dock spanning
    // the window would run underneath her feet.
    dock_ = new WorkspaceDock(this);
    connect(dock_, &WorkspaceDock::commandRequested, this, &HomeView::commandRequested);
}

// The stage: her column on the right, the caption column beside it.
//
// She gets a fixed share of the width rather than a fixed number of pixels, so
// the composition holds from a small window to a full screen -- and a floor
// under it, because below a certain width there is no room for a person and a
// sentence side by side, and the sentence has to win.
void HomeView::layoutStage() {
    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0) return;

    const int stageW = qBound(300, static_cast<int>(w * 0.40), 620);
    const bool roomForBoth = w >= 860;

#ifdef MIMI_HAS_AVATAR
    if (avatar_ != nullptr) {
        // The whole window, not a column.
        //
        // A canvas the size of her column has edges, and a transparent widget
        // still clips whatever crosses them -- her hair on a turn, the light
        // under her feet, and anything she is panned toward. That clipped
        // rectangle is the black box: she was not standing in the window, she
        // was standing in a hole cut out of it. Given the whole window she can
        // be moved anywhere in it and nothing has an edge to hit; the camera
        // offset in avatar.js is what puts her on the right by default.
        avatar_->setGeometry(0, 0, w, h);
        avatar_->setVisible(true);
        // Never lower() this one. QWebEngineView is backed by a native NSView,
        // and Qt's stacking between native and non-native siblings on macOS is
        // not dependable: lowering it put her behind the window's own backing
        // and she disappeared from the screen entirely. The page is transparent
        // everywhere she is not drawn, so the captions painted underneath show
        // through it anyway -- which is the result lower() was reaching for.
        avatar_->raise();
    }
#endif
    if (orb_ != nullptr && orb_->isVisibleTo(this)) {
        orb_->move(w - stageW / 2 - orb_->width() / 2, h / 2 - orb_->height() / 2);
    }

    // The caption column. Capped as well as inset: 40px type set across a
    // full-screen window makes a line you have to track with your head.
    const int left = 56;
    const int right = roomForBoth ? w - stageW - 24 : w - 56;
    const int colW = qBound(220, right - left, 720);

    // isVisibleTo(), never isVisible(): the latter is false for every child
    // until the window itself is shown, so during construction and the first
    // resize it reported every label hidden, measured them all as zero-height
    // and left them stacked at the origin.
    const bool showSaid = saidLabel_->isVisibleTo(this) && !saidLabel_->text().isEmpty();
    const bool showReply = replyLabel_->isVisibleTo(this);
    const bool showLive = live_->isVisibleTo(this);

    constexpr int kStateH = 18;
    constexpr int kGapState = 20;
    constexpr int kGapSaid = 16;

    const int saidH = showSaid ? saidLabel_->heightForWidth(colW) : 0;
    const int replyH = showReply ? replyLabel_->heightForWidth(colW) : 0;
    const int liveH = showLive ? live_->sizeHint().height() : 0;
    const int block = kStateH + kGapState + (showSaid ? saidH + kGapSaid : 0) +
                      qMax(replyH, liveH);

    // Centred against her, lifted slightly: optical centre sits above the
    // geometric one, and the dock occupies the bottom of the column.
    const int y0 = qMax((h - block) / 2 - 30, 84);
    int y = y0;

    stateLabel_->setGeometry(left, y, colW, kStateH);
    y += kStateH + kGapState;
    if (showSaid) {
        saidLabel_->setGeometry(left, y, colW, saidH);
        y += saidH + kGapSaid;
    }
    if (showReply) replyLabel_->setGeometry(left, y, colW, replyH);
    if (showLive) live_->setGeometry(left, y, colW, liveH);

    // sizeHint() is read before the chips have been laid out for the width
    // they are about to get, so it comes back short and the row ends up
    // clipped. The floor is the tag plus a chip plus the gap between them.
    dock_->adjustSize();
    const int dockH = qMax(dock_->sizeHint().height(), 74);
    dock_->setGeometry(left, h - dockH - 34, colW, dockH);
    // The only children with anything to click, so they are the only ones that
    // have to sit above her.
    dock_->raise();
}

void HomeView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutStage();
}

void HomeView::setPresence(Presence presence) {
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
    // The block's height just changed; recentre it.
    layoutStage();
}

void HomeView::setThinking() {
    // Swap the answer slot for the live-thinking pipeline.
    replyLabel_->setVisible(false);
    live_->setVisible(true);
    live_->start();
    layoutStage();
}

}  // namespace mimi::ui
