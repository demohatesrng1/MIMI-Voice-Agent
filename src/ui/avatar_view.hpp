#pragma once

#include "ui/presence.hpp"

#include <QString>
#include <QVector>
#include <QWidget>

class QTimer;
class QWebEngineView;

namespace mimi::ui {

// Mimi's body: the VRM, rendered live.
//
// Deliberately the same interface as VoiceOrb -- setPresence() and setLevel()
// and nothing else -- so HomeView can hold one or the other without caring
// which. When there is no .vrm on disk, or the build has no Qt WebEngine,
// available() is false and the app falls back to the 2D orb, exactly as the
// voice falls back to AVSpeech when VOICEVOX is not installed.
//
// The rendering itself is a Qt WebEngine page running three.js and three-vrm
// (src/ui/web/). That is not a shortcut: three-vrm already implements MToon,
// the VRMC_springBone simulation, the expression manager and eye lookAt, and
// re-implementing those four against Qt Quick 3D would be a rendering project
// on its own. The page is served from a private `mimi:` scheme with no network
// access -- see AvatarScheme.
class AvatarView : public QWidget {
    Q_OBJECT

public:
    // One mora of speech, straight from the VOICEVOX audio query: when it
    // starts, how long it lasts, and which of a/i/u/e/o it is ('\0' for a
    // silent or nasal mora, which closes the mouth).
    struct Mora {
        double t = 0;
        double length = 0;
        char vowel = '\0';
    };

    // False when this build has no WebEngine or no model was found, in which
    // case constructing one is pointless.
    static bool available();
    // The .vrm actually in use, for the settings page. Empty when there is none.
    static QString modelPath();

    explicit AvatarView(QWidget* parent = nullptr);
    ~AvatarView() override;

    QSize sizeHint() const override { return {360, 460}; }
    QSize minimumSizeHint() const override { return {240, 300}; }

public Q_SLOTS:
    void setPresence(Presence presence);
    void setLevel(float rms);
    // Lip sync. The timeline comes from the synthesiser's own prosody plan, so
    // the mouth matches the audio by construction rather than by chasing the
    // waveform. `delay` is how long until the first sample is audible.
    void playVisemes(const QVector<Mora>& track, double delay);
    // Barge-in and Stop both land here: her mouth has to close the moment the
    // audio is cut.
    void clearVisemes();

Q_SIGNALS:
    // The model finished loading and she is on screen.
    void ready();
    // Something went wrong; the host should put the 2D orb back.
    void failed();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // Runs a snippet against window.mimiAvatar, or drops it if the page is not
    // up yet. Every call is a target rather than an event, so dropping one
    // while loading is always safe.
    void call(const QString& js);
    // Feeds the cursor position in so she keeps watching the pointer once it
    // leaves the view -- inside it the page tracks the pointer itself.
    void trackGaze();

    QWebEngineView* web_ = nullptr;
    QTimer* gaze_ = nullptr;
    bool ready_ = false;
    Presence presence_ = Presence::Observing;
    float level_ = -1.0f;
};

}  // namespace mimi::ui
