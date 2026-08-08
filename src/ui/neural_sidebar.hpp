#pragma once

#include <QString>
#include <QWidget>

class QVBoxLayout;

namespace mimi::ui {

// The control side of the command centre.
//
// One floating glass panel rather than a page of rows: capsule cards for the
// permissions, each with a switch that glows when it is on, then the voice and
// model it is running, then the notes count as a chip at the foot.
//
// Everything here is *live state of this machine* -- macOS permissions, the
// synthesiser actually in use, the model actually pulled. Nothing on this panel
// is a setting that only exists in the app's own head, which is why it can sit
// permanently on screen without becoming wallpaper.
class NeuralSidebar : public QWidget {
    Q_OBJECT

public:
    explicit NeuralSidebar(QWidget* parent = nullptr);

    // Re-reads every permission and count. Cheap enough to call on a timer.
    void refresh();

Q_SIGNALS:
    // A row asked to open the macOS pane that grants it.
    void permissionRequested(int which);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    class Capsule;
    class NeonSwitch;

    QVBoxLayout* column_ = nullptr;
    Capsule* accessibility_ = nullptr;
    Capsule* microphone_ = nullptr;
    Capsule* contacts_ = nullptr;
    Capsule* screen_ = nullptr;
    Capsule* speech_ = nullptr;
    Capsule* model_ = nullptr;
    QWidget* notesChip_ = nullptr;
    QString notesText_;
};

}  // namespace mimi::ui
