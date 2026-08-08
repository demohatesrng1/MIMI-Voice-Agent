#include "ui/neural_sidebar.hpp"

#include "brain/accessibility.hpp"
#include "brain/notes.hpp"
#include "brain/ollama.hpp"
#include "ui/theme.hpp"
#include "voice/tts.hpp"

#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace mimi::ui {
namespace {

QColor alpha(QColor colour, double a) {
    colour.setAlphaF(std::clamp(a, 0.0, 1.0));
    return colour;
}

}  // namespace

// A switch that emits light when it is on. Off, it is a hairline outline with a
// dim knob; on, it fills with cyan and casts a short glow -- the only thing on
// the panel bright enough to find without reading.
class NeuralSidebar::NeonSwitch : public QWidget {
public:
    explicit NeonSwitch(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(42, 22);
    }

    void setOn(bool on) {
        if (on_ == on) return;
        on_ = on;
        update();
    }
    bool isOn() const { return on_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
        const double r = box.height() / 2.0;

        if (on_) {
            // The glow, painted as three widening strokes rather than a blur --
            // Qt Widgets has no shadow primitive and this is cheaper than one.
            for (int ring = 3; ring >= 1; --ring) {
                QPen halo(alpha(theme::kAccent, 0.055 * ring));
                halo.setWidthF(ring * 2.2);
                painter.setPen(halo);
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(box, r, r);
            }
        }

        painter.setPen(QPen(on_ ? alpha(theme::kAccent, 0.9)
                                : QColor(255, 255, 255, 40), 1.0));
        painter.setBrush(on_ ? alpha(theme::kAccent, 0.22) : QColor(255, 255, 255, 12));
        painter.drawRoundedRect(box, r, r);

        painter.setPen(Qt::NoPen);
        painter.setBrush(on_ ? theme::kAccentSoft : QColor(255, 255, 255, 90));
        const double knob = box.height() - 6;
        const double x = on_ ? box.right() - knob - 3 : box.left() + 3;
        painter.drawEllipse(QRectF(x, box.top() + 3, knob, knob));
    }

private:
    bool on_ = false;
};

// One row of the panel, as its own glass capsule.
class NeuralSidebar::Capsule : public QWidget {
public:
    Capsule(QString name, QString note, bool withSwitch, QWidget* parent = nullptr)
        : QWidget(parent), name_(std::move(name)), note_(std::move(note)) {
        setMinimumHeight(withSwitch ? 58 : 52);
        if (withSwitch) {
            toggle_ = new NeonSwitch(this);
        }
    }

    void setOn(bool on) { if (toggle_) toggle_->setOn(on); }
    void setValue(const QString& value) { value_ = value; update(); }

protected:
    void resizeEvent(QResizeEvent*) override {
        if (toggle_) toggle_->move(width() - toggle_->width() - 14,
                                   (height() - toggle_->height()) / 2);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

        painter.setPen(QPen(QColor(255, 255, 255, 20), 1.0));
        painter.setBrush(QColor(255, 255, 255, 10));
        painter.drawRoundedRect(box, 14, 14);

        painter.setPen(theme::kInk);
        QFont name = font();
        name.setPointSizeF(12.5);
        name.setWeight(QFont::Medium);
        painter.setFont(name);
        painter.drawText(QRect(14, 10, width() - 80, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, name_);

        painter.setPen(theme::kFaint);
        QFont note = font();
        note.setPointSizeF(10.5);
        painter.setFont(note);
        const int noteWidth = width() - (toggle_ ? 76 : 24);
        painter.drawText(QRect(14, height() - 26, noteWidth, 16),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(note).elidedText(note_, Qt::ElideRight, noteWidth));

        // A value instead of a switch, for the things that are not on/off.
        if (!value_.isEmpty()) {
            painter.setPen(theme::kAccentSoft);
            QFont v = font();
            v.setPointSizeF(11.0);
            v.setWeight(QFont::Medium);
            painter.setFont(v);
            painter.drawText(QRect(width() - 170, 10, 156, 18),
                             Qt::AlignRight | Qt::AlignVCenter, value_);
        }
    }

private:
    QString name_;
    QString note_;
    QString value_;
    NeonSwitch* toggle_ = nullptr;
};

NeuralSidebar::NeuralSidebar(QWidget* parent) : QWidget(parent) {
    column_ = new QVBoxLayout(this);
    column_->setContentsMargins(18, 20, 18, 18);
    column_->setSpacing(9);

    const auto heading = [this](const QString& text) {
        auto* label = new QLabel(text, this);
        label->setStyleSheet(
            QStringLiteral("color:%1; font-size:10px; font-weight:700; letter-spacing:3px;")
                .arg(theme::kFaint.name()));
        column_->addWidget(label);
    };

    heading(QStringLiteral("PERMISSIONS"));
    accessibility_ = new Capsule(QStringLiteral("Accessibility"),
                                 QStringLiteral("Press buttons, read your window"), true, this);
    microphone_ = new Capsule(QStringLiteral("Microphone"),
                              QStringLiteral("The wake word, and everything after"), true, this);
    contacts_ = new Capsule(QStringLiteral("Contacts"),
                            QStringLiteral("Only when you ask her to call"), true, this);
    screen_ = new Capsule(QStringLiteral("Screen Recording"),
                          QStringLiteral("Required by macOS for screenshots"), true, this);
    for (Capsule* row : {accessibility_, microphone_, contacts_, screen_}) column_->addWidget(row);

    column_->addSpacing(12);
    heading(QStringLiteral("VOICE AND THINKING"));
    speech_ = new Capsule(QStringLiteral("Speech"), QStringLiteral("How she answers"), false, this);
    model_ = new Capsule(QStringLiteral("Model"), QStringLiteral("What she thinks with"), false, this);
    column_->addWidget(speech_);
    column_->addWidget(model_);

    column_->addStretch(1);

    // The notes chip. Small on purpose: it is a count, not a control.
    notesChip_ = new QWidget(this);
    notesChip_->setFixedHeight(26);
    column_->addWidget(notesChip_, 0, Qt::AlignLeft);

    refresh();
}

void NeuralSidebar::refresh() {
    using brain::ax::Access;
    const auto granted = [](Access access) { return access == Access::Granted; };

    accessibility_->setOn(brain::ax::has_permission());
    microphone_->setOn(granted(brain::ax::microphone_access()));
    contacts_->setOn(granted(brain::ax::contacts_access()));
    screen_->setOn(granted(brain::ax::screen_recording_access()));

    // What she is actually running, not what is configured.
#ifdef MIMI_HAS_VOICEVOX
    speech_->setValue(QStringLiteral("VOICEVOX"));
#else
    speech_->setValue(QStringLiteral("System"));
#endif
    model_->setValue(QString::fromStdString(brain::Ollama::Config{}.model));

    notesText_ = QStringLiteral("%1 NOTES").arg(brain::Notes().all(999).size());
    notesChip_->update();
    update();
}

void NeuralSidebar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // The panel itself: glass floating over the void, with a brighter hairline
    // along the top edge where light would catch it.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 12));
    painter.drawRoundedRect(box, 22, 22);

    QLinearGradient edge(box.topLeft(), box.bottomLeft());
    edge.setColorAt(0.0, QColor(255, 255, 255, 46));
    edge.setColorAt(0.5, QColor(255, 255, 255, 20));
    edge.setColorAt(1.0, QColor(255, 255, 255, 12));
    painter.setPen(QPen(QBrush(edge), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(box, 22, 22);

    // The notes chip, drawn here so it sits on the panel rather than in it.
    if (notesChip_ != nullptr && !notesText_.isEmpty()) {
        QFont chip = font();
        chip.setPointSizeF(9.5);
        chip.setWeight(QFont::DemiBold);
        painter.setFont(chip);
        const QRect text = QFontMetrics(chip).boundingRect(notesText_);
        const QRectF pill(notesChip_->x(), notesChip_->y(), text.width() + 22, 24);
        painter.setPen(QPen(alpha(theme::kLive, 0.55), 1.0));
        painter.setBrush(alpha(theme::kLive, 0.14));
        painter.drawRoundedRect(pill, 12, 12);
        painter.setPen(theme::kAccentSoft);
        painter.drawText(pill, Qt::AlignCenter, notesText_);
    }
}

}  // namespace mimi::ui
