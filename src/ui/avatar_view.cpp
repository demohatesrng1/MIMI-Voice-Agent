#include "ui/avatar_view.hpp"

#include "core/log.hpp"
#include "ui/avatar_scheme.hpp"

#include <QCursor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace mimi::ui {
namespace {

constexpr const char* kTag = "avatar";

// The presence names the page understands. Strings rather than the enum's
// integers so a mismatch shows up in a log line instead of posing her wrong.
QString presence_name(Presence p) {
    switch (p) {
        case Presence::Observing:   return QStringLiteral("observing");
        case Presence::Listening:   return QStringLiteral("listening");
        case Presence::Thinking:    return QStringLiteral("thinking");
        case Presence::Speaking:    return QStringLiteral("speaking");
        case Presence::Remembering: return QStringLiteral("remembering");
        case Presence::Muted:       return QStringLiteral("muted");
    }
    return QStringLiteral("observing");
}

// A page that reports what the JavaScript says. No Q_OBJECT: it adds no
// signals of its own, only an override, so it needs no moc pass.
class AvatarPage : public QWebEnginePage {
public:
    using QWebEnginePage::QWebEnginePage;

    std::function<void()> onReady;
    std::function<void()> onFailed;

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString& message,
                                  int lineNumber, const QString& sourceId) override {
        Q_UNUSED(lineNumber);
        Q_UNUSED(sourceId);
        if (message.startsWith(QStringLiteral("mimi-avatar: ready"))) {
            if (onReady) onReady();
            return;
        }
        if (message.startsWith(QStringLiteral("mimi-avatar: load failed"))) {
            log::error(kTag, "{}", message.toStdString());
            if (onFailed) onFailed();
            return;
        }
        // Anything else the page says goes to the app's log rather than
        // nowhere -- a WebGL failure is otherwise completely silent.
        if (level == ErrorMessageLevel) {
            log::warn(kTag, "page: {}", message.toStdString());
        } else {
            log::debug(kTag, "page: {}", message.toStdString());
        }
    }
};

}  // namespace

bool AvatarView::available() { return !modelPath().isEmpty(); }

QString AvatarView::modelPath() {
    // Resolved once: the answer cannot change while the app runs, and the
    // search touches the filesystem.
    static const QString path = AvatarScheme::findModel();
    return path;
}

AvatarView::AvatarView(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    web_ = new QWebEngineView(this);
    auto* page = new AvatarPage(QWebEngineProfile::defaultProfile(), web_);
    page->onReady = [this] {
        if (ready_) return;
        ready_ = true;
        log::info(kTag, "avatar ready");
        // Everything set while she was loading is a target, so replaying the
        // current one is enough to catch up.
        setPresence(presence_);
        Q_EMIT ready();
    };
    page->onFailed = [this] { Q_EMIT failed(); };
    web_->setPage(page);

    // The page is composited over the living background, so both the widget
    // and the page have to be transparent -- either one left opaque cuts a
    // rectangle out of the ambient canvas.
    web_->setAttribute(Qt::WA_TranslucentBackground);
    page->setBackgroundColor(Qt::transparent);

    // It is a rendering surface, not a browser: no menu, no selection, no
    // navigation, and nothing it could reach off the machine anyway.
    web_->setContextMenuPolicy(Qt::NoContextMenu);
    auto* settings = page->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    settings->setAttribute(QWebEngineSettings::ShowScrollBars, false);
    settings->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    settings->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);

    AvatarScheme::install(QWebEngineProfile::defaultProfile(), modelPath());
    layout->addWidget(web_);

    log::info(kTag, "model {}", modelPath().toStdString());
    web_->load(QUrl(QStringLiteral("mimi://avatar/index.html?model=model.vrm")));

    // She keeps watching the pointer even when it is somewhere else in the
    // window. 30 Hz is far below the render rate and costs a cursor query.
    gaze_ = new QTimer(this);
    gaze_->setInterval(33);
    connect(gaze_, &QTimer::timeout, this, &AvatarView::trackGaze);
}

AvatarView::~AvatarView() = default;

void AvatarView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    gaze_->start();
}

void AvatarView::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    // Hidden means the window is closed to the puck. Nothing to look at, and
    // no reason to keep waking up.
    gaze_->stop();
}

void AvatarView::call(const QString& js) {
    if (!ready_ || web_ == nullptr) return;
    web_->page()->runJavaScript(js);
}

void AvatarView::setPresence(Presence presence) {
    presence_ = presence;
    call(QStringLiteral("window.mimiAvatar.setPresence('%1')").arg(presence_name(presence)));
}

void AvatarView::setLevel(float rms) {
    // The level arrives ~12 times a second and each call crosses into the
    // renderer process. Below a fiftieth there is nothing to see, so silence
    // costs nothing at all.
    if (qAbs(rms - level_) < 0.02f) return;
    level_ = rms;
    call(QStringLiteral("window.mimiAvatar.setLevel(%1)").arg(static_cast<double>(rms)));
}

void AvatarView::trackGaze() {
    if (!ready_ || !isVisible()) return;
    const QPoint local = mapFromGlobal(QCursor::pos());
    const QSizeF size = this->size();
    if (size.width() <= 0 || size.height() <= 0) return;
    // -1..1 across the widget, clamped: the pointer is usually outside her, and
    // letting the value run away would have her staring at the far wall.
    const double x = qBound(-1.5, local.x() / size.width() * 2.0 - 1.0, 1.5);
    const double y = qBound(-1.5, -(local.y() / size.height() * 2.0 - 1.0), 1.5);
    call(QStringLiteral("window.mimiAvatar.setGaze(%1,%2)").arg(x).arg(y));
}

void AvatarView::playVisemes(const QVector<Mora>& track, double delay) {
    if (track.isEmpty()) return;
    QJsonArray array;
    for (const Mora& mora : track) {
        QJsonObject entry;
        entry[QStringLiteral("t")] = mora.t;
        entry[QStringLiteral("length")] = mora.length;
        entry[QStringLiteral("vowel")] =
            mora.vowel == '\0' ? QString() : QString(QChar::fromLatin1(mora.vowel));
        array.append(entry);
    }
    const QString json =
        QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    call(QStringLiteral("window.mimiAvatar.playVisemes(%1,%2)").arg(json).arg(delay));
}

void AvatarView::clearVisemes() {
    call(QStringLiteral("window.mimiAvatar.clearVisemes()"));
}

}  // namespace mimi::ui
