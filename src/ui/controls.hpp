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
