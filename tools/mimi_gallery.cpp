// Offscreen render harness: composes the real UI widgets the way the app layers
// them (living background behind a page), lets the animations settle for a beat,
// and writes a PNG per view. It never touches the microphone, the models, or the
// native window chrome, so it verifies the drawing in isolation -- run it under
// QT_QPA_PLATFORM=offscreen and inspect the images.

#include "ui/ai_dock.hpp"
#include "ui/ambient.hpp"
#include "ui/canvas_view.hpp"
#include "ui/command_palette.hpp"
#include "ui/context_ribbon.hpp"
#include "ui/digital_twin.hpp"
#include "ui/home_view.hpp"
#include "ui/live_thinking.hpp"
#include "ui/mission_control.hpp"
#include "ui/neural_search.hpp"
#include "ui/presence.hpp"
#include "ui/timeline_view.hpp"
#include "ui/workspace_dock.hpp"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLineEdit>
#include <QPixmap>
#include <QTimer>
#include <QVBoxLayout>

using namespace mimi::ui;

namespace {

const QSize kStage(1180, 740);

void pump(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();  // real time passes, so the animations advance
}

// A page over the living background, exactly as MainWindow stacks them.
AmbientCanvas* stage(QWidget* page, Presence presence) {
    auto* ambient = new AmbientCanvas;
    ambient->resize(kStage);
    ambient->setPresence(presence);
    auto* column = new QVBoxLayout(ambient);
    column->setContentsMargins(0, 0, 0, 0);
    column->addWidget(page);
    ambient->show();
    return ambient;
}

// The same, with the context ribbon under the chrome -- the full composed app.
AmbientCanvas* stageWithRibbon(QWidget* page, Presence presence) {
    auto* ambient = new AmbientCanvas;
    ambient->resize(kStage);
    ambient->setPresence(presence);
    auto* column = new QVBoxLayout(ambient);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(new ContextRibbon);
    column->addWidget(page);
    ambient->show();
    return ambient;
}

void save(QWidget* widget, const QString& dir, const QString& name) {
    QPixmap pm(widget->size());
    pm.fill(Qt::black);
    widget->render(&pm);
    pm.save(dir + "/" + name);
    qInfo().noquote() << "  wrote" << name;
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QFile qss(QStringLiteral(MIMI_SRC_DIR "/src/ui/theme.qss"));
    if (qss.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    const QString out = argc > 1 ? QString::fromUtf8(argv[1])
                                 : QStringLiteral("/tmp/mimi_gallery");
    QDir().mkpath(out);

    // 1) Home at rest -- orb alive, predictive actions offered, context ribbon
    //    on show, background near-black.
    {
        auto* home = new HomeView;
        home->setPresence(Presence::Observing);
        auto* s = stageWithRibbon(home, Presence::Observing);
        pump(1300);
        save(s, out, "01_home_observing.png");
    }

    // 2) Home mid-answer -- an exchange, a confidence read-out, and the dock
    //    switched to the Coding toolset.
    {
        auto* home = new HomeView;
        home->setPresence(Presence::Speaking);
        home->setExchange(QString::fromUtf8("バッテリーはどのくらい"),
                          QString::fromUtf8("87パーセントです。あと4時間ほど使えます。"));
        home->setConfidence(0.9);
        home->workspace()->setContext(WorkspaceDock::Context::Coding);
        auto* s = stageWithRibbon(home, Presence::Speaking);
        pump(1300);
        save(s, out, "02_home_answer.png");
    }

    // 2b) Home while thinking -- the live pipeline building an answer.
    {
        auto* home = new HomeView;
        home->setExchange(QString::fromUtf8("この関数のバグを直して"), QString());
        home->setThinking();
        home->setPresence(Presence::Thinking);
        auto* s = stageWithRibbon(home, Presence::Thinking);
        pump(950);  // let a couple of pipeline stages fill
        save(s, out, "09_home_thinking.png");
    }

    // 3) The living background, one file per motion signature.
    struct Shot {
        Presence presence;
        float level;
        const char* name;
    };
    const Shot shots[] = {
        {Presence::Listening, 0.6f, "03_ambient_listening.png"},
        {Presence::Thinking, 0.0f, "04_ambient_thinking.png"},
        {Presence::Speaking, 0.5f, "05_ambient_responding.png"},
        {Presence::Remembering, 0.0f, "06_ambient_remembering.png"},
    };
    for (const Shot& shot : shots) {
        auto* ambient = new AmbientCanvas;
        ambient->resize(kStage);
        ambient->setPresence(shot.presence);
        ambient->setLevel(shot.level);
        ambient->show();
        pump(1500);
        save(ambient, out, shot.name);
    }

    // 4) The infinite canvas, seeded with a connected cluster.
    {
        auto* canvas = new CanvasView;
        auto* s = stage(canvas, Presence::Observing);
        pump(900);
        save(s, out, "07_canvas.png");
    }

    // 5) The memory timeline.
    {
        auto* timeline = new TimelineView;
        timeline->remember(QString::fromUtf8("今日の予定を教えて"),
                           QString::fromUtf8("午後2時に打ち合わせが一件あります。"));
        auto* s = stage(timeline, Presence::Observing);
        pump(900);
        save(s, out, "08_timeline.png");
    }

    // 6) The AI dock, floating over the home surface.
    {
        auto* home = new HomeView;
        home->setPresence(Presence::Observing);
        auto* s = stageWithRibbon(home, Presence::Observing);
        auto* dock = new AiDock(s);
        dock->adjustSize();
        dock->move(16, (s->height() - dock->height()) / 2);
        dock->show();
        dock->raise();
        pump(700);
        save(s, out, "10_ai_dock.png");
    }

    // 7) The command palette, summoned over the workspace.
    {
        auto* home = new HomeView;
        home->setPresence(Presence::Observing);
        auto* s = stageWithRibbon(home, Presence::Observing);
        auto* dock = new AiDock(s);
        dock->adjustSize();
        dock->move(16, (s->height() - dock->height()) / 2);
        dock->show();
        dock->raise();
        auto* palette = new CommandPalette(s);
        palette->open();  // resizes to the stage, shows, focuses, fills the list
        pump(500);
        save(s, out, "11_command_palette.png");
    }

    // 8) Mission Control: a mission opened, its workspace assembled card by card.
    {
        auto* missions = new MissionControl;
        auto* s = stageWithRibbon(missions, Presence::Thinking);
        missions->openMission(0);  // a click would do this
        pump(2900);                // let all seven step cards land
        save(s, out, "12_mission_control.png");
    }

    // 9) Neural search, understanding a description rather than a filename.
    {
        auto* home = new HomeView;
        home->setPresence(Presence::Observing);
        auto* s = stageWithRibbon(home, Presence::Observing);
        auto* search = new NeuralSearch(s);
        search->open();
        // Seed a query so the ranked results show.
        if (auto* field = s->findChild<QLineEdit*>(QStringLiteral("paletteField")))
            field->setText(QStringLiteral("the deck John liked"));
        pump(400);
        save(s, out, "13_neural_search.png");
    }

    // 10) Adaptive UI: the same home in Simple mode -- ribbon compact, dock gone.
    {
        auto* home = new HomeView;
        home->setPresence(Presence::Observing);
        auto* ambient = new AmbientCanvas;
        ambient->resize(kStage);
        ambient->setPresence(Presence::Observing);
        auto* col = new QVBoxLayout(ambient);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(0);
        auto* ribbon = new ContextRibbon;
        ribbon->setCompact(true);  // Simple mode
        col->addWidget(ribbon);
        col->addWidget(home);
        ambient->show();
        pump(700);
        save(ambient, out, "14_adaptive_simple.png");
    }

    // 11) Digital Twin: what she's learned about you.
    {
        auto* twin = new DigitalTwin;
        auto* s = stageWithRibbon(twin, Presence::Observing);
        pump(700);
        save(s, out, "15_digital_twin.png");
    }

    qInfo().noquote() << "gallery ->" << out;
    return 0;
}
