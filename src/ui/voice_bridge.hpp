#pragma once

#include "voice/listener.hpp"

#include <QObject>
#include <QString>

namespace mimi::ui {

// The thread boundary between the voice engine and the GUI.
//
// Listener's callbacks fire on the audio and worker threads, and touching a
// QWidget from either of those is undefined behaviour. This object turns each
// callback into a Qt signal; because the bridge lives on the GUI thread, Qt's
// auto-connection queues every emission across to it. Nothing downstream of
// here has to think about threads.
class VoiceBridge : public QObject {
    Q_OBJECT

public:
    explicit VoiceBridge(QObject* parent = nullptr) : QObject(parent) {}

    // Installs the callbacks. Call before Listener::start().
    void attach(voice::Listener& listener) {
        listener.on_state([this](voice::State state) {
            Q_EMIT stateChanged(static_cast<int>(state));
        });
        listener.on_level([this](float rms, float wake) {
            Q_EMIT levelChanged(rms, wake);
        });
        listener.on_utterance([this](std::string text, bool follow_up) {
            Q_EMIT heard(QString::fromStdString(text), follow_up);
        });
        listener.on_barge_in([this] { Q_EMIT bargedIn(); });
    }

Q_SIGNALS:
    void stateChanged(int state);
    void levelChanged(float rms, float wake);
    void heard(QString text, bool followUp);
    void bargedIn();
};

}  // namespace mimi::ui
