#pragma once

#include "ui/icons.hpp"

#include <QAbstractButton>
#include <QString>

class QVariantAnimation;

namespace mimi::ui {

// An icon-only button that is invisible until it matters: no chrome at rest,
// a soft raised fill as the cursor arrives, the accent when checked. All of
// the app's secondary actions look like this, which is what keeps the chrome
// calm -- controls announce themselves on approach, not all at once.
class GhostButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit GhostButton(icons::Glyph glyph, QWidget* parent = nullptr);

    void setGlyph(icons::Glyph glyph);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    void animateTo(qreal target);

    icons::Glyph glyph_;
    qreal hover_ = 0.0;
    QVariantAnimation* anim_ = nullptr;
};

// A calm confidence read-out: a hairline track that fills toward the accent,
// with the figure beside it. "Done" tells you nothing; a product that has
// reasoned about an answer can say how sure it is. Hidden until there is an
// answer to qualify, and it fills on its own clock so the number feels earned.
class ConfidenceMeter : public QWidget {
    Q_OBJECT

public:
    explicit ConfidenceMeter(QWidget* parent = nullptr);

    QSize sizeHint() const override;

    // 0..1 to show and animate to; a negative value clears the meter.
    void setConfidence(qreal value);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    qreal shown_ = 0.0;    // animated fill
    bool active_ = false;
    QVariantAnimation* anim_ = nullptr;
};

// Adaptive UI, as one control: a two-state segmented pill, Simple ↔ Expert.
// The interface evolves with the user -- Simple hides the power surfaces,
// Expert brings them all out -- and this is the switch that drives it.
class ModeToggle : public QWidget {
    Q_OBJECT

public:
    explicit ModeToggle(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    bool expert() const noexcept { return expert_; }
    void setExpert(bool expert);

Q_SIGNALS:
    void toggled(bool expert);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool expert_ = true;
    qreal pos_ = 1.0;  // 0 = Simple, 1 = Expert, animated
    QVariantAnimation* anim_ = nullptr;
};

// A suggestion: a quiet pill of text that lifts a pixel on hover. Painted,
// not styled, because the lift and the fade have to move on the same clock.
class Chip : public QAbstractButton {
    Q_OBJECT

public:
    explicit Chip(const QString& text, QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    void animateTo(qreal target);

    qreal hover_ = 0.0;
    QVariantAnimation* anim_ = nullptr;
};

}  // namespace mimi::ui
