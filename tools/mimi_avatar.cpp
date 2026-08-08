// The avatar on its own: no microphone, no models, no account gate.
//
//   mimi_avatar            cycle every presence, three seconds each
//   mimi_avatar --check    load, wait for her to come up, print and exit
//   mimi_avatar listening  hold one presence
//
// --check is the one that belongs in a build script: it exits non-zero if the
// model cannot be found, the mimi: scheme cannot serve it, three-vrm cannot
// parse it or WebGL is unavailable, which is every way the avatar can fail
// short of looking wrong.

#include "core/log.hpp"
#include "ui/avatar_scheme.hpp"
#include "ui/avatar_view.hpp"
#include "ui/presence.hpp"
#include "ui/theme.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

using namespace mimi::ui;

namespace {

const struct {
    const char* name;
    Presence presence;
} kStates[] = {
    {"observing", Presence::Observing},   {"listening", Presence::Listening},
    {"thinking", Presence::Thinking},     {"speaking", Presence::Speaking},
    {"remembering", Presence::Remembering}, {"muted", Presence::Muted},
};

}  // namespace

int main(int argc, char** argv) {
    mimi::log::configure_from_env();
    AvatarScheme::registerScheme();
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mimi_avatar"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Mimi's avatar, on its own.");
    parser.addHelpOption();
    QCommandLineOption check(QStringLiteral("check"),
                             QStringLiteral("Exit 0 once she is on screen, 1 if she never is."));
    parser.addOption(check);
    parser.addPositionalArgument(QStringLiteral("presence"),
                                 QStringLiteral("Hold one presence instead of cycling."));
    parser.process(app);

    if (!AvatarView::available()) {
        std::fprintf(stderr,
                     "no .vrm found. Put one at ~/Library/Application Support/Mimi/avatar.vrm,\n"
                     "in the repo root, or point MIMI_AVATAR_MODEL at it.\n");
        return 1;
    }
    std::fprintf(stderr, "model: %s\n", AvatarView::modelPath().toUtf8().constData());

    QWidget window;
    window.setWindowTitle(QStringLiteral("Mimi — avatar"));
    window.setStyleSheet(QStringLiteral("background: #171718;"));
    auto* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* avatar = new AvatarView;
    layout->addWidget(avatar);
    window.resize(420, 540);
    window.show();

    QElapsedTimer clock;
    clock.start();

    const bool checking = parser.isSet(check);
    QObject::connect(avatar, &AvatarView::ready, &app, [&clock, checking] {
        std::fprintf(stderr, "ready in %lld ms\n", static_cast<long long>(clock.elapsed()));
        if (checking) QApplication::exit(0);
    });
    QObject::connect(avatar, &AvatarView::failed, &app, [] {
        std::fprintf(stderr, "the page failed to load the model\n");
        QApplication::exit(1);
    });

    // A model this size parses in a second or two; well past that and
    // something is wrong rather than slow.
    if (checking) {
        QTimer::singleShot(30000, &app, [] {
            std::fprintf(stderr, "timed out waiting for the avatar\n");
            QApplication::exit(1);
        });
    }

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        for (const auto& state : kStates) {
            if (args.first() == QLatin1String(state.name)) avatar->setPresence(state.presence);
        }
    } else if (!checking) {
        // Cycle, so every pose can be looked at without driving the app.
        auto* timer = new QTimer(&window);
        auto* index = new int(0);
        QObject::connect(timer, &QTimer::timeout, avatar, [avatar, index] {
            const auto& state = kStates[*index % std::size(kStates)];
            std::fprintf(stderr, "%s\n", state.name);
            avatar->setPresence(state.presence);
            ++*index;
        });
        timer->start(3000);
    }

    return app.exec();
}
