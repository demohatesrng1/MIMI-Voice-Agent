#pragma once

#include <QString>
#include <QWidget>

namespace mimi::ui {

// Choosing which face she wears.
//
// One row of circular portraits, the chosen one ringed in the accent. Used in
// two places and identical in both: once during sign-up, and again in Settings
// so the choice is never final -- a decision made in ten seconds on the first
// run should not be permanent.
//
// It shows her as a circle because that is exactly how she will appear
// afterwards. A picker that previews a square and then ships a circle is how
// people end up choosing the one whose head gets cropped.
class FacePicker : public QWidget {
    Q_OBJECT

public:
    explicit FacePicker(QWidget* parent = nullptr);

    QString selected() const { return selected_; }
    // Selects without emitting, for seeding from the saved account.
    void setSelected(const QString& id);

Q_SIGNALS:
    // Emitted on click. The caller decides whether to persist it: sign-up waits
    // until the account exists, Settings writes it immediately.
    void chosen(QString id);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    // The circle rect for a tile, in widget coordinates.
    QRect tileRect(int index) const;

    QString selected_;
    int hovered_ = -1;
};

}  // namespace mimi::ui
