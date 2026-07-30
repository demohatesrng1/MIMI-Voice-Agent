#pragma once

#include <QWidget>

class QLineEdit;
class QListWidget;

namespace mimi::ui {

// Command everything: one surface for every action, summoned with ⌘K.
//
// A single intelligent command bar is more futuristic than a wall of buttons.
// Type a few letters, and the thing you want -- run a control, jump to a
// surface, ask a question -- is one Return away. Everything the app can do is
// reachable here without ever hunting a menu.
class CommandPalette : public QWidget {
    Q_OBJECT

public:
    explicit CommandPalette(QWidget* parent = nullptr);

    void open();   // show, focus, reset to the full list
    void dismiss();

Q_SIGNALS:
    void commandChosen(QString utterance);  // run through the router
    void navigateChosen(int page);          // jump to a surface (Page enum)

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refilter(const QString& query);
    void activateCurrent();

    QWidget* panel_ = nullptr;
    QLineEdit* field_ = nullptr;
    QListWidget* list_ = nullptr;
};

}  // namespace mimi::ui
