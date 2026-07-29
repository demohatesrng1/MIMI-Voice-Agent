// Mimi -- the desktop app.

#include "core/log.hpp"
#include "core/paths.hpp"
#include "ui/main_window.hpp"

#include <QApplication>
#include <QFile>
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

    mimi::log::info("app", "data {}", mimi::paths::data_dir().string());
    mimi::log::info("app", "models {}", mimi::paths::models_dir().string());

    mimi::ui::MainWindow window;
    window.show();

    // After show(), so the window is on screen before the models load.
    window.startVoice();

    return app.exec();
}
