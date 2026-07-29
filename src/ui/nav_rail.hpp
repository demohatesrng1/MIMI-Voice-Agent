#pragma once

#include <QString>
#include <QWidget>
#include <vector>

class QButtonGroup;
class QLabel;
class QPushButton;

namespace mimi::ui {

// The left sidebar.
//
// Labelled, not icon-only. An icon rail works in VS Code because its four
// destinations are universal and used hourly; here the destinations are
// specific to this app and used occasionally, so a bare glyph is a guessing
// game. The width buys unambiguous navigation.
//
// The lower half is an engine readout -- which models are actually loaded.
// For a local assistant that is genuinely useful information: it is the
// difference between "she is thinking" and "nothing is running".
class NavRail : public QWidget {
    Q_OBJECT

public:
    enum Page { Home = 0, Activity, Skills, Settings };

    explicit NavRail(QWidget* parent = nullptr);

    void setCurrent(int page);
    void setListening(bool listening);

    // Engine status lines. Empty text greys the row out as "not running".
    void setSpeechEngine(const QString& name, bool ready);
    void setBrainEngine(const QString& name, bool ready);
    void setVoiceEngine(const QString& name, bool ready);

Q_SIGNALS:
    void pageSelected(int page);
    void muteToggled(bool muted);

private:
    QWidget* buildStatusRow(const QString& caption, QLabel** valueOut, QLabel** dotOut);
    QPushButton* addTab(const QString& label, int page);

    QButtonGroup* group_ = nullptr;
    QPushButton* power_ = nullptr;
    std::vector<QPushButton*> tabs_;

    QLabel* speechDot_ = nullptr;
    QLabel* speechValue_ = nullptr;
    QLabel* brainDot_ = nullptr;
    QLabel* brainValue_ = nullptr;
    QLabel* voiceDot_ = nullptr;
    QLabel* voiceValue_ = nullptr;
};

}  // namespace mimi::ui
