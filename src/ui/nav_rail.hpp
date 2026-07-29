#pragma once

#include <QWidget>
#include <vector>

class QButtonGroup;
class QPushButton;

namespace mimi::ui {

// The left navigation rail.
//
// A voice assistant needs somewhere to *go*, not just a scrollback. Four
// destinations, always visible, always in the same place: where she is now,
// what she has done, what she can do, and how she is set up.
class NavRail : public QWidget {
    Q_OBJECT

public:
    enum Page { Home = 0, Activity, Skills, Settings };

    explicit NavRail(QWidget* parent = nullptr);

    void setCurrent(int page);
    // The mute control lives here too, pinned to the bottom, away from the
    // navigation so it is never hit by accident.
    void setListening(bool listening);

Q_SIGNALS:
    void pageSelected(int page);
    void muteToggled(bool muted);

private:
    QPushButton* addTab(const QString& tip, int page);

    QButtonGroup* group_ = nullptr;
    QPushButton* power_ = nullptr;
    std::vector<QPushButton*> tabs_;
};

}  // namespace mimi::ui
