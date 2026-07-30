// Mimi -- the desktop app.

#include "core/log.hpp"
#include "core/paths.hpp"
#include "brain/account.hpp"
#include "ui/account_view.hpp"
#include "ui/main_window.hpp"

#include <QApplication>
#include <QFile>
#include <QEventLoop>
#include <QIcon>
#include <QFontDatabase>

namespace {

QString load_stylesheet() {
    QFile file(QStringLiteral(":/theme.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

}  // namespace

int main(int argc, char** argv) {
    mimi::log::configure_from_env();

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Mimi"));
    app.setOrganizationName(QStringLiteral("Mimi"));
    app.setApplicationDisplayName(QStringLiteral("Mimi ミミ"));
    app.setStyleSheet(load_stylesheet());
    // Also set at runtime: the bundle icon covers Finder and the Dock, this
    // covers the window and the app switcher when run from a build tree.
    app.setWindowIcon(QIcon(QStringLiteral(":/mimi_512.png")));

    mimi::log::info("app", "data {}", mimi::paths::data_dir().string());
    mimi::log::info("app", "models {}", mimi::paths::models_dir().string());

    // The account gate comes first. MainWindow is not constructed until someone
    // is in -- building it early would start the microphone and the wake word
    // behind a login screen, which is the one place she should not be
    // listening.
    {
        mimi::ui::AccountView gate;
        gate.setWindowTitle(QStringLiteral("Mimi"));
        gate.resize(880, 620);

        bool entered = false;
        QObject::connect(&gate, &mimi::ui::AccountView::authenticated,
                         [&entered, &gate](const QString&) {
                             entered = true;
                             gate.close();
                         });
        gate.show();
        // A nested run of the same event loop, not a second application: the
        // gate owns the screen until it is satisfied, and quitting it here
        // leaves the QApplication intact for the window that follows.
        QEventLoop loop;
        QObject::connect(&gate, &mimi::ui::AccountView::authenticated, &loop,
                         &QEventLoop::quit);
        QObject::connect(&gate, &mimi::ui::AccountView::abandoned, &loop,
                         &QEventLoop::quit);
        loop.exec();
        if (!entered) return 0;  // closed the window instead of signing in
    }

    mimi::ui::MainWindow window;
    // winId() forces the NSWindow into existence without mapping it, so the
    // style mask can be changed before AppKit computes the content view's
    // frame. Doing this after show() sets the flags correctly but leaves the
    // content still inset below the title bar, which is how the window ended
    // up with two stacked bars.
    window.winId();
    window.applyNativeChrome();
    window.show();

    // After show(), so the window is on screen before the models load.
    window.startVoice();

    return app.exec();
}
