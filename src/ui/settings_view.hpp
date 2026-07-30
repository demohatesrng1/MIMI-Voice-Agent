#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QVBoxLayout;

namespace mimi::ui {

// What Mimi needs from the system, and whether she has it.
//
// This replaced a mocked-up "digital twin" panel that displayed invented facts
// about the user. The honest version of that surface is this one: the
// permissions she actually depends on, where her data actually lives, and what
// is actually missing -- with a way to fix each thing that is not granted.
//
// macOS grants these one dialog at a time and never reports back, so the state
// is re-read whenever the page is shown rather than cached.
class SettingsView : public QWidget {
    Q_OBJECT

public:
    explicit SettingsView(QWidget* parent = nullptr);

    // Re-reads every permission and path. Called on show.
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;

private:
    // One row: a name, a live state, and an optional button that fixes it.
    struct Row {
        QLabel* value = nullptr;
        QLabel* detail = nullptr;
    };

    Row addRow(QVBoxLayout* into, const QString& name, const QString& hint,
               const QString& action, std::function<void()> on_action);
    void addHeading(QVBoxLayout* into, const QString& text);

    Row account_;
    Row accessibility_;
    Row microphone_;
    Row contacts_;
    Row screen_;
    Row voice_;
    Row brain_;
    Row notes_;
    Row dataDir_;
    Row modelsDir_;
};

}  // namespace mimi::ui
