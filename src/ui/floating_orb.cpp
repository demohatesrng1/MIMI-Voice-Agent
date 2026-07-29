#include "ui/floating_orb.hpp"

#include "ui/theme.hpp"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace mimi::ui {
namespace {

constexpr int kSize = 76;      // the puck itself
constexpr int kPadding = 10;   // room for the glow to spill without clipping
constexpr int kDragSlop = 4;   // px before a press counts as a drag
constexpr int kEdgeMargin = 18;

}  // namespace

FloatingOrb::FloatingOrb(QWidget* parent) : QWidget(parent) {
    // Tool rather than Window: no Dock tile, no app-switcher entry, and it
    // floats above full-screen apps.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                   Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);  // never steal focus
    setFixedSize(kSize + kPadding * 2, kSize + kPadding * 2);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Mimi — click to open, double-click to talk"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPadding, kPadding, kPadding, kPadding);
    orb_ = new VoiceOrb(this);
    orb_->setFixedSize(kSize, kSize);
    orb_->setAttribute(Qt::WA_TransparentForMouseEvents);  // the puck handles input
    layout->addWidget(orb_);

    menu_ = new QMenu(this);
    auto* open = menu_->addAction(QStringLiteral("ウィンドウを開く   Open window"));
    connect(open, &QAction::triggered, this, &FloatingOrb::clicked);

    auto* talk = menu_->addAction(QStringLiteral("話しかける   Talk now"));
    connect(talk, &QAction::triggered, this, &FloatingOrb::doubleClicked);

    menu_->addSeparator();
    auto* mute = menu_->addAction(QStringLiteral("マイクをオフ   Mute microphone"));
    mute->setCheckable(true);
    connect(mute, &QAction::toggled, this, [this](bool on) {
        muted_ = on;
        Q_EMIT muteRequested(on);
        update();
    });

    menu_->addSeparator();
    auto* quit = menu_->addAction(QStringLiteral("終了   Quit Mimi"));
    connect(quit, &QAction::triggered, this, &FloatingOrb::quitRequested);
}

void FloatingOrb::setState(int state) { orb_->setState(state); }
void FloatingOrb::setLevel(float rms) { orb_->setLevel(rms); }

void FloatingOrb::moveToDefaultCorner() {
    const QScreen* screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) return;
    const QRect area = screen->availableGeometry();
    // Bottom right, clear of the Dock's usual position.
    move(area.right() - width() - kEdgeMargin, area.bottom() - height() - kEdgeMargin * 4);
}

void FloatingOrb::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF disc(kPadding, kPadding, kSize, kSize);

    // A dark backing plate, so the orb stays legible over a bright window.
    QColor plate = theme::kBg;
    plate.setAlphaF(hovered_ ? 0.92 : 0.78);
    painter.setPen(Qt::NoPen);
    painter.setBrush(plate);
    painter.drawEllipse(disc);

    // Hairline edge; red while muted, so "she cannot hear you" is unmistakable
    // without opening anything.
    QColor edge = muted_ ? QColor(0xe0, 0x5a, 0x7a) : theme::kViolet;
    edge.setAlphaF(hovered_ ? 0.85 : 0.45);
    painter.setPen(QPen(edge, hovered_ ? 1.6 : 1.1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(disc.adjusted(0.5, 0.5, -0.5, -0.5));

    if (muted_) {
        // A slash across the puck. Cheaper to read at 76px than an icon.
        QPen slash(QColor(0xe0, 0x5a, 0x7a, 200), 2.4);
        slash.setCapStyle(Qt::RoundCap);
        painter.setPen(slash);
        const qreal inset = kSize * 0.28;
        painter.drawLine(QPointF(disc.left() + inset, disc.top() + inset),
                         QPointF(disc.right() - inset, disc.bottom() - inset));
    }
}

void FloatingOrb::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    dragging_ = true;
    moved_beyond_threshold_ = false;
    press_offset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
}

void FloatingOrb::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    const QPoint target = event->globalPosition().toPoint() - press_offset_;
    if ((target - pos()).manhattanLength() > kDragSlop) moved_beyond_threshold_ = true;
    move(target);
}

void FloatingOrb::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    dragging_ = false;
    // A press that never really moved was a click, not a tiny drag.
    if (!moved_beyond_threshold_) {
        Q_EMIT clicked();
        return;
    }
    snapToEdge();
}

void FloatingOrb::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) Q_EMIT doubleClicked();
}

void FloatingOrb::contextMenuEvent(QContextMenuEvent* event) {
    menu_->exec(event->globalPos());
}

void FloatingOrb::enterEvent(QEnterEvent*) {
    hovered_ = true;
    update();
}

void FloatingOrb::leaveEvent(QEvent*) {
    hovered_ = false;
    update();
}

void FloatingOrb::snapToEdge() {
    const QScreen* screen = QGuiApplication::screenAt(frameGeometry().center());
    if (screen == nullptr) screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) return;

    const QRect area = screen->availableGeometry();
    QPoint target = pos();

    // Pull to whichever vertical edge is closer, and keep it fully on screen.
    const int distance_left = target.x() - area.left();
    const int distance_right = area.right() - (target.x() + width());
    target.setX(distance_left <= distance_right ? area.left() + kEdgeMargin
                                                : area.right() - width() - kEdgeMargin);
    target.setY(std::clamp(target.y(), area.top() + kEdgeMargin,
                           area.bottom() - height() - kEdgeMargin));
    move(target);
}

}  // namespace mimi::ui
