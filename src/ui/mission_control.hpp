#pragma once

#include <QWidget>

class QLabel;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

namespace mimi::ui {

// Mission Control -- the hero surface.
//
// You do not open apps, you open a mission: "Prepare tomorrow's client
// meeting." She then assembles it in front of you -- pulls the latest mail,
// the relevant documents, the last meeting's summary, the open action items,
// drafts an agenda, talking points, a deck -- each landing as its own card, in
// sequence, so one living workspace replaces jumping between five programs.
// The system does more while showing less.
class MissionControl : public QWidget {
    Q_OBJECT

public:
    explicit MissionControl(QWidget* parent = nullptr);

    // Open a mission and begin assembling its board. Public so it can be driven
    // programmatically (a click normally triggers it).
    void openMission(int index);

private:
    QWidget* buildLauncher();
    QWidget* buildBoard();
    void revealNext();

    QStackedWidget* stack_ = nullptr;
    QLabel* boardTitle_ = nullptr;
    QLabel* boardStatus_ = nullptr;
    QVBoxLayout* steps_ = nullptr;  // where assembled step cards are added
    QTimer* reveal_ = nullptr;
    int revealed_ = 0;
};

}  // namespace mimi::ui
