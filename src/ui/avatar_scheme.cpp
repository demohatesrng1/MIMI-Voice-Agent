#include "ui/avatar_scheme.hpp"

#include "core/log.hpp"
#include "core/paths.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

namespace mimi::ui {
namespace {

constexpr const char* kTag = "avatar";
constexpr const char* kSchemeName = "mimi";

// One qrc file per URL path. Everything else is refused rather than mapped, so
// the page cannot walk out of the two files it is meant to have.
struct Asset {
    const char* path;
    const char* resource;
    const char* mime;
};

constexpr Asset kAssets[] = {
    {"/",                 ":/avatar/index.html",       "text/html"},
    {"/index.html",       ":/avatar/index.html",       "text/html"},
    {"/avatar.bundle.js", ":/avatar/avatar.bundle.js", "text/javascript"},
};

}  // namespace

void AvatarScheme::registerScheme() {
    // Qt refuses registration once WebEngine has initialised, and calling it
    // twice is an error, so this guards itself -- MainWindow can be built more
    // than once in the gallery harness.
    static bool done = false;
    if (done) return;
    done = true;

    QWebEngineUrlScheme scheme(kSchemeName);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setDefaultPort(QWebEngineUrlScheme::PortUnspecified);
    // Secure, so the page counts as a trustworthy origin. The other two are
    // not optional despite everything here being same-origin: three.js loads
    // the model through fetch(), and Chromium refuses fetch on a scheme that
    // is not explicitly marked for it -- "URL scheme mimi is not supported",
    // with the model never arriving. CorsEnabled alone does not cover it;
    // FetchApiAllowed is a separate flag.
    auto flags = QWebEngineUrlScheme::SecureScheme |
                 QWebEngineUrlScheme::CorsEnabled |
                 QWebEngineUrlScheme::LocalAccessAllowed;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    flags |= QWebEngineUrlScheme::FetchApiAllowed;
#endif
    scheme.setFlags(flags);
    QWebEngineUrlScheme::registerScheme(scheme);
}

QString AvatarScheme::findModel() {
    const auto exists = [](const QString& path) {
        return !path.isEmpty() && QFileInfo::exists(path);
    };

    if (const QByteArray env = qgetenv("MIMI_AVATAR_MODEL"); !env.isEmpty()) {
        const QString path = QString::fromLocal8Bit(env);
        if (exists(path)) return path;
        log::warn(kTag, "MIMI_AVATAR_MODEL is set but {} does not exist", path.toStdString());
    }

    // The name the app expects once she is installed properly, in both places
    // it keeps large files.
    for (const auto& dir : {paths::models_dir(), paths::data_dir()}) {
        const QString path = QString::fromStdString((dir / "avatar.vrm").string());
        if (exists(path)) return path;
    }

    // Inside a bundle.
    const QString bundled =
        QString::fromStdString((paths::exe_dir() / ".." / "Resources" / "avatar.vrm").string());
    if (exists(bundled)) return bundled;

    // Running from a build tree: take any .vrm sitting in the checkout. The
    // model is gitignored and named by whoever exported it, so this is the
    // only way to find it without making the developer rename a file.
    QDir dir(QString::fromStdString(paths::exe_dir().string()));
    for (int up = 0; up < 5; ++up) {
        const auto found = dir.entryList({"*.vrm"}, QDir::Files, QDir::Name);
        if (!found.isEmpty()) return dir.filePath(found.first());
        if (!dir.cdUp()) break;
    }
    return {};
}

AvatarScheme::AvatarScheme(QString modelPath, QObject* parent)
    : QWebEngineUrlSchemeHandler(parent), model_(std::move(modelPath)) {}

void AvatarScheme::install(QWebEngineProfile* profile, const QString& modelPath) {
    if (profile == nullptr) return;
    profile->installUrlSchemeHandler(QByteArray(kSchemeName),
                                     new AvatarScheme(modelPath, profile));
}

void AvatarScheme::requestStarted(QWebEngineUrlRequestJob* job) {
    const QString path = job->requestUrl().path().isEmpty() ? QStringLiteral("/")
                                                            : job->requestUrl().path();

    const auto serve = [job](const QString& file, const char* mime) {
        auto* device = new QFile(file, job);
        if (!device->open(QIODevice::ReadOnly)) {
            log::warn(kTag, "cannot open {}", file.toStdString());
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        // The job owns the device and closes it when the reply is done, which
        // matters for the model: it is 17 MB and streamed, not slurped.
        job->reply(QByteArray(mime), device);
    };

    for (const auto& asset : kAssets) {
        if (path == QLatin1String(asset.path)) {
            serve(QString::fromLatin1(asset.resource), asset.mime);
            return;
        }
    }

    if (path == QLatin1String("/model.vrm")) {
        if (model_.isEmpty()) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        serve(model_, "model/gltf-binary");
        return;
    }

    log::warn(kTag, "refused {}", path.toStdString());
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
}

}  // namespace mimi::ui
