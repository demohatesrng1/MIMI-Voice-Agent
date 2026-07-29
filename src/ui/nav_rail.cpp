#include "ui/nav_rail.hpp"

#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include <QButtonGroup>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace mimi::ui {
namespace {

constexpr int kRailWidth = 186;

// A sidebar row that paints its own icon, label and selection bar.
//
// Painted rather than styled because the selection bar has to sit hard against
// the panel edge, outside any padding a stylesheet would impose -- and because
// the icon has to be tinted to match the label, which QSS cannot do to a QIcon.
class RailTab : public QPushButton {
public:
    RailTab(icons::Glyph glyph, const QString& label, QWidget* parent = nullptr)
        : QPushButton(parent), glyph_(glyph), label_(label) {
        setObjectName(QStringLiteral("navTab"));
        setCheckable(true);
        setFixedHeight(34);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const bool on = isChecked();
        const bool hot = underMouse();
        const QColor tint = on ? theme::kPink : (hot ? theme::kInk : QColor(0x6a, 0x6a, 0x82));

        if (on || hot) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(on ? QColor(255, 45, 135, 22) : QColor(255, 255, 255, 10));
            // Square-ish: sharp corners read as precise at 1x, where a large
            // radius just looks like a soft smear.
            painter.drawRoundedRect(QRectF(8, 1, width() - 16.0, height() - 2.0), 4, 4);
        }
        if (on) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(theme::kPink);
            painter.drawRect(QRectF(0, 7, 2, height() - 14.0));
        }

        icons::icon(glyph_, tint, 16).paint(&painter, QRect(20, (height() - 16) / 2, 16, 16));

        QFont font = painter.font();
        font.setPixelSize(12);
        font.setWeight(on ? QFont::DemiBold : QFont::Normal);
        painter.setFont(font);
        painter.setPen(tint);
        painter.drawText(QRect(46, 0, width() - 56, height()),
                         Qt::AlignVCenter | Qt::AlignLeft, label_);
    }

private:
    icons::Glyph glyph_;
    QString label_;
};

QLabel* caption(const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("railCaption"));
    return label;
}

}  // namespace

NavRail::NavRail(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("rail"));
    setFixedWidth(kRailWidth);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 14, 0, 12);
    layout->setSpacing(0);

    auto* navCaption = caption(QStringLiteral("NAVIGATION"));
    navCaption->setContentsMargins(20, 0, 0, 0);
    layout->addWidget(navCaption);
    layout->addSpacing(8);

    group_ = new QButtonGroup(this);
    group_->setExclusive(true);
    layout->addWidget(addTab(QStringLiteral("Home"), Home));
    layout->addWidget(addTab(QStringLiteral("Activity"), Activity));
    layout->addWidget(addTab(QStringLiteral("Skills"), Skills));
    layout->addWidget(addTab(QStringLiteral("Settings"), Settings));

    layout->addStretch(1);

    // What is actually loaded. For a local assistant this is the difference
    // between "thinking" and "nothing is running", and it is otherwise
    // invisible until something fails.
    auto* engineCaption = caption(QStringLiteral("ENGINE"));
    engineCaption->setContentsMargins(20, 0, 0, 0);
    layout->addWidget(engineCaption);
    layout->addSpacing(7);

    layout->addWidget(buildStatusRow(QStringLiteral("SPEECH"), &speechValue_, &speechDot_));
    layout->addWidget(buildStatusRow(QStringLiteral("BRAIN"), &brainValue_, &brainDot_));
    layout->addWidget(buildStatusRow(QStringLiteral("VOICE"), &voiceValue_, &voiceDot_));

    layout->addSpacing(12);

    power_ = new QPushButton(QStringLiteral("MIC LIVE"));
    power_->setObjectName(QStringLiteral("power"));
    power_->setCheckable(true);
    power_->setChecked(true);
    power_->setFixedHeight(32);
    power_->setIconSize(QSize(14, 14));
    power_->setIcon(icons::icon(icons::Glyph::Mic, theme::kPink, 14));
    power_->setCursor(Qt::PointingHandCursor);
    power_->setToolTip(QStringLiteral("Mute the microphone"));
    connect(power_, &QPushButton::toggled, this,
            [this](bool on) { Q_EMIT muteToggled(!on); });

    auto* powerHolder = new QWidget;
    auto* powerLayout = new QVBoxLayout(powerHolder);
    powerLayout->setContentsMargins(14, 0, 14, 0);
    powerLayout->addWidget(power_);
    layout->addWidget(powerHolder);

    tabs_.front()->setChecked(true);
    setSpeechEngine(QString(), false);
    setBrainEngine(QString(), false);
    setVoiceEngine(QString(), false);
}

QWidget* NavRail::buildStatusRow(const QString& text, QLabel** valueOut, QLabel** dotOut) {
    auto* row = new QWidget;
    row->setFixedHeight(21);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(20, 0, 16, 0);
    layout->setSpacing(7);

    auto* dot = new QLabel(QStringLiteral("●"));
    dot->setObjectName(QStringLiteral("engineDot"));
    layout->addWidget(dot);

    auto* name = new QLabel(text);
    name->setObjectName(QStringLiteral("engineKey"));
    name->setFixedWidth(46);
    layout->addWidget(name);

    auto* value = new QLabel(QStringLiteral("—"));
    value->setObjectName(QStringLiteral("engineValue"));
    layout->addWidget(value, 1);

    *valueOut = value;
    *dotOut = dot;
    return row;
}

QPushButton* NavRail::addTab(const QString& label, int page) {
    static const icons::Glyph kGlyphs[]{icons::Glyph::Home, icons::Glyph::Activity,
                                        icons::Glyph::Skills, icons::Glyph::Settings};
    auto* button = new RailTab(kGlyphs[page], label);
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
    power_->setText(listening ? QStringLiteral("MIC LIVE") : QStringLiteral("MUTED"));
    power_->setIcon(icons::icon(icons::Glyph::Mic,
                                listening ? theme::kPink : theme::kError, 14));
    power_->setToolTip(listening ? QStringLiteral("Mute the microphone")
                                 : QStringLiteral("Unmute the microphone"));
}

namespace {
void apply_status(QLabel* dot, QLabel* value, const QString& name, bool ready) {
    value->setText(name.isEmpty() ? QStringLiteral("not running") : name);
    dot->setStyleSheet(QStringLiteral("color:%1;")
                           .arg(ready ? QStringLiteral("#ff2d87")
                                      : QStringLiteral("#33334a")));
    value->setStyleSheet(QStringLiteral("color:%1;")
                             .arg(ready ? QStringLiteral("#8f8fa8")
                                        : QStringLiteral("#43435a")));
}
}  // namespace

void NavRail::setSpeechEngine(const QString& name, bool ready) {
    apply_status(speechDot_, speechValue_, name, ready);
}
void NavRail::setBrainEngine(const QString& name, bool ready) {
    apply_status(brainDot_, brainValue_, name, ready);
}
void NavRail::setVoiceEngine(const QString& name, bool ready) {
    apply_status(voiceDot_, voiceValue_, name, ready);
}

}  // namespace mimi::ui
