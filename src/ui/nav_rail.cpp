#include "ui/nav_rail.hpp"

#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include <QButtonGroup>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace mimi::ui {
namespace {

// A rail tab that paints its own selection bar.
//
// The bar is what makes an activity rail read as navigation rather than as a
// column of buttons -- VS Code, Slack and Xcode all use one, and without it a
// highlighted background alone is ambiguous with "hovered".
class RailTab : public QPushButton {
public:
    RailTab(icons::Glyph glyph, const QString& tip, QWidget* parent = nullptr)
        : QPushButton(parent), glyph_(glyph) {
        setObjectName(QStringLiteral("navTab"));
        setCheckable(true);
        setFixedSize(48, 42);
        setCursor(Qt::PointingHandCursor);
        setToolTip(tip);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const bool on = isChecked();
        const QColor tint = on ? theme::kPink
                               : (underMouse() ? theme::kInk : QColor(0x55, 0x55, 0x6a));

        if (underMouse() && !on) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 12));
            painter.drawRoundedRect(QRectF(7, 3, width() - 14.0, height() - 6.0), 9, 9);
        }

        // The selection bar, hard against the left edge.
        if (on) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(theme::kPink);
            painter.drawRoundedRect(QRectF(0, 8, 2.5, height() - 16.0), 1.2, 1.2);
        }

        const int size = 21;
        const QRect box((width() - size) / 2 + 1, (height() - size) / 2, size, size);
        icons::icon(glyph_, tint, size).paint(&painter, box);
    }

private:
    icons::Glyph glyph_;
};

}  // namespace

NavRail::NavRail(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("rail"));
    setFixedWidth(58);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 12, 0, 14);
    layout->setSpacing(2);

    group_ = new QButtonGroup(this);
    group_->setExclusive(true);

    layout->addWidget(addTab(QStringLiteral("Home"), Home), 0, Qt::AlignHCenter);
    layout->addWidget(addTab(QStringLiteral("Activity"), Activity), 0, Qt::AlignHCenter);
    layout->addWidget(addTab(QStringLiteral("Skills"), Skills), 0, Qt::AlignHCenter);
    layout->addWidget(addTab(QStringLiteral("Settings"), Settings), 0, Qt::AlignHCenter);

    layout->addStretch(1);

    power_ = new QPushButton;
    power_->setObjectName(QStringLiteral("power"));
    power_->setCheckable(true);
    power_->setChecked(true);
    power_->setFixedSize(32, 32);
    power_->setIconSize(QSize(17, 17));
    power_->setIcon(icons::icon(icons::Glyph::Power, theme::kPink, 17));
    power_->setCursor(Qt::PointingHandCursor);
    power_->setToolTip(QStringLiteral("Mute the microphone"));
    connect(power_, &QPushButton::toggled, this,
            [this](bool on) { Q_EMIT muteToggled(!on); });
    layout->addWidget(power_, 0, Qt::AlignHCenter);

    tabs_.front()->setChecked(true);
}

QPushButton* NavRail::addTab(const QString& tip, int page) {
    static const icons::Glyph kGlyphs[]{icons::Glyph::Home, icons::Glyph::Activity,
                                        icons::Glyph::Skills, icons::Glyph::Settings};
    auto* button = new RailTab(kGlyphs[page], tip);
    group_->addButton(button, page);
    connect(button, &QPushButton::clicked, this, [this, page] { Q_EMIT pageSelected(page); });
    tabs_.push_back(button);
    return button;
}

void NavRail::setCurrent(int page) {
    if (page >= 0 && page < static_cast<int>(tabs_.size())) tabs_[page]->setChecked(true);
}

void NavRail::setListening(bool listening) {
    const bool blocked = power_->blockSignals(true);
    power_->setChecked(listening);
    power_->blockSignals(blocked);
    power_->setIcon(icons::icon(icons::Glyph::Power,
                                listening ? theme::kPink : theme::kError, 17));
    power_->setToolTip(listening ? QStringLiteral("Mute the microphone")
                                 : QStringLiteral("Unmute the microphone"));
}

}  // namespace mimi::ui
