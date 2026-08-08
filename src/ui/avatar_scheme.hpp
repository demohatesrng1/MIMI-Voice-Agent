#pragma once

#include <QByteArray>
#include <QString>
#include <QWebEngineUrlSchemeHandler>

class QWebEngineProfile;

namespace mimi::ui {

// Serves the avatar page to Qt WebEngine over a private `mimi:` scheme.
//
// Two things have to reach the same page from different places: the HTML and
// the JavaScript bundle, which are compiled into the binary as Qt resources,
// and the .vrm itself, which is a licensed 17 MB file that lives on disk and
// is deliberately never committed or embedded. Loading the page from a
// file:// or qrc:// URL and fetching the model from the other would be a
// cross-origin request that Chromium refuses.
//
// A scheme of our own puts all three behind one origin, so there is no CORS
// question to answer and no temporary copy of the page on disk. It is also
// registered without CorsEnabled and without any network access, so the page
// cannot reach anything but what is handed to it here -- which is the property
// an assistant that promises nothing leaves the machine should have.
class AvatarScheme : public QWebEngineUrlSchemeHandler {
    Q_OBJECT

public:
    // Must run before QApplication is constructed; Qt refuses scheme
    // registration once WebEngine has started. Safe to call more than once.
    static void registerScheme();

    // Where the .vrm was found, or empty when there is none -- which is how
    // the app decides whether it has a body to show at all. Checked in order:
    // $MIMI_AVATAR_MODEL, <models>/avatar.vrm, <data>/avatar.vrm, then any
    // *.vrm beside the executable or up the tree in a build checkout.
    static QString findModel();

    explicit AvatarScheme(QString modelPath, QObject* parent = nullptr);

    void requestStarted(QWebEngineUrlRequestJob* job) override;

    // Installs a handler on the profile. The profile takes no ownership, so
    // the handler is parented to it.
    static void install(QWebEngineProfile* profile, const QString& modelPath);

private:
    QString model_;
};

}  // namespace mimi::ui
