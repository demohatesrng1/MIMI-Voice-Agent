#pragma once

#include <QGraphicsItem>
#include <QGraphicsView>
#include <QString>
#include <QVector>

class QGraphicsScene;

namespace mimi::ui {

class CanvasCard;

// A soft connector between two cards, drawn behind them. It reads relationship
// without demanding attention -- a curve, not an arrow -- and re-routes itself
// whenever either card moves.
class CanvasEdge : public QGraphicsItem {
public:
    CanvasEdge(CanvasCard* from, CanvasCard* to);

    void adjust();  // recompute from the current card positions
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

private:
    CanvasCard* from_;
    CanvasCard* to_;
    QPointF a_;
    QPointF b_;
};

// One object on the canvas: an idea, a chat, a note, a snippet, a voice memo.
// A raised glass tile you can pick up and move; its edges follow. Everything is
// the same material a step up the graphite ramp, so a wall of them still reads
// as one surface rather than a ransom note.
class CanvasCard : public QGraphicsItem {
public:
    enum class Kind { Idea, Chat, Note, Code, Voice, Image };

    CanvasCard(Kind kind, const QString& title, const QString& body);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) override;

    void addEdge(CanvasEdge* edge) { edges_.push_back(edge); }
    QPointF centre() const { return mapToScene(boundingRect().center()); }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent*) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override;

private:
    Kind kind_;
    QString title_;
    QString body_;
    QVector<CanvasEdge*> edges_;
    bool hover_ = false;
};

// The infinite canvas: one endless surface where everything lives and connects,
// instead of a stack of pages. Drag empty space to move through it, scroll to
// zoom, double-click to drop a note wherever you are looking. FigJam, with an
// assistant already in the room.
class CanvasView : public QGraphicsView {
    Q_OBJECT

public:
    explicit CanvasView(QWidget* parent = nullptr);

    // Drop a card onto the canvas near the middle of the current view.
    CanvasCard* addNear(CanvasCard::Kind kind, const QString& title, const QString& body);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    CanvasCard* add(CanvasCard::Kind kind, const QString& title, const QString& body,
                    const QPointF& scenePos);
    void link(CanvasCard* a, CanvasCard* b);
    void seed();
    void buildChrome();

    QGraphicsScene* scene_ = nullptr;
    bool panning_ = false;
    QPoint panFrom_;
    qreal zoom_ = 1.0;
};

}  // namespace mimi::ui
