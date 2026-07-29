// Mimi -- the desktop app.

#include "core/log.hpp"
#include "core/paths.hpp"
#include "ui/main_window.hpp"

#include <QApplication>
#include <QFile>
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
