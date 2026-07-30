#include "ui/canvas_view.hpp"

#include "ui/controls.hpp"
#include "ui/theme.hpp"

#include <QGraphicsDropShadowEffect>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace mimi::ui {
namespace {

constexpr qreal kCardW = 200.0;
constexpr qreal kCardH = 122.0;
constexpr qreal kGrid = 40.0;

struct KindStyle {
    const char* name;
    QColor dot;
};

KindStyle styleFor(CanvasCard::Kind kind) {
    switch (kind) {
        case CanvasCard::Kind::Idea:  return {"IDEA",  theme::kAccent};
        case CanvasCard::Kind::Chat:  return {"CHAT",  theme::kAccentSoft};
        case CanvasCard::Kind::Note:  return {"NOTE",  theme::kDim};
        case CanvasCard::Kind::Code:  return {"CODE",  theme::kAccentGlow};
        case CanvasCard::Kind::Voice: return {"VOICE", theme::kAccent};
        case CanvasCard::Kind::Image: return {"IMAGE", theme::kAccentSoft};
    }
    return {"NOTE", theme::kDim};
}

}  // namespace

// ------------------------------------------------------------------ CanvasEdge

CanvasEdge::CanvasEdge(CanvasCard* from, CanvasCard* to) : from_(from), to_(to) {
    setZValue(0.0);  // beneath the cards
    adjust();
}

void CanvasEdge::adjust() {
    prepareGeometryChange();
    a_ = from_->centre();
    b_ = to_->centre();
}

QRectF CanvasEdge::boundingRect() const {
    return QRectF(a_, b_).normalized().adjusted(-40, -40, 40, 40);
}

void CanvasEdge::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    // A horizontal S-curve: the control points reach out sideways, so the line
    // leaves each card level and reads as a considered connection.
    const qreal midx = (a_.x() + b_.x()) * 0.5;
    QPainterPath path(a_);
    path.cubicTo(QPointF(midx, a_.y()), QPointF(midx, b_.y()), b_);

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);

    // A soft wide underlay for glow, then a thin bright core over it.
    QColor glow = theme::kAccent;
    glow.setAlphaF(0.06);
    painter->setPen(QPen(glow, 6.0));
    painter->drawPath(path);

    QColor core = theme::kAccentSoft;
    core.setAlphaF(0.30);
    painter->setPen(QPen(core, 1.4));
    painter->drawPath(path);

    // A node where each end meets its card, so the join looks intentional.
    painter->setPen(Qt::NoPen);
    painter->setBrush(core);
    painter->drawEllipse(a_, 2.4, 2.4);
    painter->drawEllipse(b_, 2.4, 2.4);
}

// ------------------------------------------------------------------ CanvasCard

CanvasCard::CanvasCard(Kind kind, const QString& title, const QString& body)
    : kind_(kind), title_(title), body_(body) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setCursor(Qt::OpenHandCursor);
    setZValue(1.0);
    // Render the card -- glass, text, and its blurred shadow -- to a pixmap once
    // and reuse it while panning. Without this the shadow is re-blurred every
    // frame the view scrolls, which is the whole cost of a graphics view.
    setCacheMode(DeviceCoordinateCache);

    // The shadow is the elevation: the card floats a real distance above the
    // surface, which is what makes the canvas feel like depth rather than a map.
    auto* shadow = new QGraphicsDropShadowEffect;
    shadow->setColor(QColor(0, 0, 0, 150));
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 10);
    setGraphicsEffect(shadow);
}

QRectF CanvasCard::boundingRect() const { return {0, 0, kCardW, kCardH}; }

void CanvasCard::hoverEnterEvent(QGraphicsSceneHoverEvent*) {
    hover_ = true;
    update();
}

void CanvasCard::hoverLeaveEvent(QGraphicsSceneHoverEvent*) {
    hover_ = false;
    update();
}

QVariant CanvasCard::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionHasChanged)
        for (CanvasEdge* edge : edges_) edge->adjust();
    return QGraphicsItem::itemChange(change, value);
}

void CanvasCard::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF r = boundingRect();
    const KindStyle style = styleFor(kind_);

    QColor glass = theme::kLayer2;
    glass.setAlpha(238);
    if (hover_ || isSelected()) glass = glass.lighter(112);
    painter->setPen(Qt::NoPen);
    painter->setBrush(glass);
    painter->drawRoundedRect(r, 14, 14);

    // Rim: the accent when selected, a lit top edge otherwise.
    if (isSelected()) {
        QColor rim = theme::kAccent;
        rim.setAlphaF(0.85);
        painter->setPen(QPen(rim, 1.6));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(r.adjusted(0.8, 0.8, -0.8, -0.8), 13, 13);
    } else {
        painter->setPen(QPen(QColor(255, 255, 255, hover_ ? 34 : 18), 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 13.5, 13.5);
    }

    // Kind tag: a dot and a tracked-out word, the quietest label that still says
    // what this is.
    painter->setBrush(style.dot);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(QPointF(18, 24), 3.0, 3.0);

    QFont tag = painter->font();
    tag.setPixelSize(9);
    tag.setWeight(QFont::DemiBold);
    tag.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    painter->setFont(tag);
    QColor tagColour = style.dot;
    tagColour.setAlphaF(0.9);
    painter->setPen(tagColour);
    painter->drawText(QRectF(28, 16, kCardW - 40, 16), Qt::AlignVCenter | Qt::AlignLeft,
                      QString::fromUtf8(style.name));

    QFont titleFont = painter->font();
    titleFont.setPixelSize(14);
    titleFont.setWeight(QFont::DemiBold);
    titleFont.setLetterSpacing(QFont::PercentageSpacing, 100.0);
    painter->setFont(titleFont);
    painter->setPen(theme::kInk);
    painter->drawText(QRectF(18, 36, kCardW - 34, 24), Qt::AlignVCenter | Qt::AlignLeft,
                      title_);

    QFont bodyFont = painter->font();
    bodyFont.setPixelSize(11);
    bodyFont.setWeight(QFont::Normal);
    painter->setFont(bodyFont);
    painter->setPen(theme::kDim);
    painter->drawText(QRectF(18, 62, kCardW - 34, kCardH - 74),
                      Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, body_);
}

// ------------------------------------------------------------------ CanvasView

CanvasView::CanvasView(QWidget* parent) : QGraphicsView(parent) {
    scene_ = new QGraphicsScene(this);
    scene_->setSceneRect(-4000, -4000, 8000, 8000);
    setScene(scene_);

    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing);
    setAttribute(Qt::WA_TranslucentBackground);
    viewport()->setAttribute(Qt::WA_TranslucentBackground);
    viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    // The dotted grid is the same every frame within a scroll position, so cache
    // it; only newly exposed strips are redrawn as you pan. Skipping painter
    // state save/restore per item shaves the rest.
    setCacheMode(QGraphicsView::CacheBackground);
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);

    seed();
    buildChrome();
    centerOn(60, 20);
}

CanvasCard* CanvasView::add(CanvasCard::Kind kind, const QString& title, const QString& body,
                            const QPointF& scenePos) {
    auto* card = new CanvasCard(kind, title, body);
    card->setPos(scenePos);
    scene_->addItem(card);
    return card;
}

void CanvasView::link(CanvasCard* a, CanvasCard* b) {
    auto* edge = new CanvasEdge(a, b);
    a->addEdge(edge);
    b->addEdge(edge);
    scene_->addItem(edge);
}

CanvasCard* CanvasView::addNear(CanvasCard::Kind kind, const QString& title,
                                const QString& body) {
    const QPointF centre = mapToScene(viewport()->rect().center());
    const QPointF jitter(QRandomGenerator::global()->bounded(120) - 60,
                         QRandomGenerator::global()->bounded(80) - 40);
    auto* card = add(kind, title, body, centre + jitter - QPointF(kCardW / 2, kCardH / 2));
    card->setSelected(true);
    return card;
}

void CanvasView::seed() {
    // A small cluster that shows the point immediately: an idea with everything
    // it spun off hanging around it, all one connected thought.
    CanvasCard* idea =
        add(CanvasCard::Kind::Idea, QStringLiteral("Launch plan"),
            QStringLiteral("Ship the assistant as a product, not a demo."), {-280, -30});
    CanvasCard* chat =
        add(CanvasCard::Kind::Chat, QStringLiteral("Thread with Mimi"),
            QStringLiteral("“Summarize what we decided about pricing.”"), {20, -170});
    CanvasCard* note =
        add(CanvasCard::Kind::Note, QStringLiteral("Positioning"),
            QStringLiteral("Calm, anticipatory, private. Not a chatbot."), {60, 70});
    CanvasCard* code =
        add(CanvasCard::Kind::Code, QStringLiteral("deploy.sh"),
            QStringLiteral("cmake --build build && open Mimi.app"), {330, -40});
    CanvasCard* voice =
        add(CanvasCard::Kind::Voice, QStringLiteral("Voice memo · 0:42"),
            QStringLiteral("Idea for the onboarding, recorded on a walk."), {-180, 150});
    CanvasCard* image =
        add(CanvasCard::Kind::Image, QStringLiteral("Hero mockup"),
            QStringLiteral("The orb, centred, alive on near-black."), {360, 150});

    link(idea, chat);
    link(idea, note);
    link(idea, voice);
    link(chat, code);
    link(chat, note);
    link(note, image);
}

void CanvasView::buildChrome() {
    // A small tool tray floating over the surface, so adding to the canvas does
    // not mean hunting for a menu.
    auto* tray = new QWidget(viewport());
    auto* row = new QHBoxLayout(tray);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    struct Add {
        const char* label;
        CanvasCard::Kind kind;
        const char* title;
        const char* body;
    };
    const Add adds[] = {
        {"New note", CanvasCard::Kind::Note, "New note", "Double-click to edit."},
        {"New idea", CanvasCard::Kind::Idea, "New idea", "What if…"},
        {"New snippet", CanvasCard::Kind::Code, "Snippet", "// paste code here"},
    };
    for (const Add& a : adds) {
        auto* chip = new Chip(QString::fromUtf8(a.label), tray);
        const auto kind = a.kind;
        const QString title = QString::fromUtf8(a.title);
        const QString body = QString::fromUtf8(a.body);
        connect(chip, &Chip::clicked, this, [this, kind, title, body] {
            addNear(kind, title, body);
        });
        row->addWidget(chip);
    }
    tray->adjustSize();
    tray->move(16, 16);

    auto* hint = new QLabel(
        QStringLiteral("Drag to pan  ·  Scroll to zoom  ·  Double-click to add"), viewport());
    hint->setObjectName(QStringLiteral("canvasHint"));
    hint->adjustSize();
    hint->move(18, 58);
}

void CanvasView::drawBackground(QPainter* painter, const QRectF& rect) {
    // A faint wash seats the cards while still letting the living background
    // breathe through from behind the whole window.
    painter->fillRect(rect, QColor(6, 7, 11, 96));

    // A dotted grid, so panning has parallax and the space feels physical. Dots,
    // not lines: quieter, and they never add up into a cage.
    const qreal left = std::floor(rect.left() / kGrid) * kGrid;
    const qreal top = std::floor(rect.top() / kGrid) * kGrid;
    QColor dot = theme::kFaint;
    dot.setAlphaF(0.22);
    painter->setPen(Qt::NoPen);
    painter->setBrush(dot);
    for (qreal x = left; x < rect.right(); x += kGrid)
        for (qreal y = top; y < rect.bottom(); y += kGrid)
            painter->drawEllipse(QPointF(x, y), 1.0, 1.0);
}

void CanvasView::mousePressEvent(QMouseEvent* event) {
    // Empty space drags the canvas; a card drags itself. Middle-button pans
    // from anywhere, even over a card.
    const bool onCard = itemAt(event->pos()) != nullptr;
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && !onCard)) {
        panning_ = true;
        panFrom_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const QPoint delta = event->pos() - panFrom_;
        panFrom_ = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_) {
        panning_ = false;
        viewport()->unsetCursor();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (itemAt(event->pos()) == nullptr) {
        const QPointF at = mapToScene(event->pos()) - QPointF(kCardW / 2, kCardH / 2);
        add(CanvasCard::Kind::Note, QStringLiteral("New note"),
            QStringLiteral("Double-click to edit."), at)
            ->setSelected(true);
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void CanvasView::wheelEvent(QWheelEvent* event) {
    // Scroll zooms toward the cursor, clamped so the canvas never inverts or
    // shrinks to a speck.
    const qreal factor = std::pow(1.0015, event->angleDelta().y());
    const qreal next = std::clamp(zoom_ * factor, 0.4, 2.4);
    const qreal applied = next / zoom_;
    zoom_ = next;
    scale(applied, applied);
}

}  // namespace mimi::ui
