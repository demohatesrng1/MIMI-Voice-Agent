#include "core/paths.hpp"

#include "core/log.hpp"

#include <cstdlib>
#include <mach-o/dyld.h>
#include <string>
#include <vector>

namespace mimi::paths {
namespace {

namespace fs = std::filesystem;

fs::path home() {
    if (const char* h = std::getenv("HOME")) return fs::path(h);
    return fs::current_path();
}

fs::path env_path(const char* key) {
    const char* v = std::getenv(key);
    return (v != nullptr && *v != '\0') ? fs::path(v) : fs::path{};
}

fs::path resolve_exe_dir() {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> raw(size + 1, '\0');
    if (_NSGetExecutablePath(raw.data(), &size) != 0) return fs::current_path();

    std::error_code ec;
    fs::path exe = fs::weakly_canonical(fs::path(raw.data()), ec);
    if (ec) exe = fs::path(raw.data());
    return exe.parent_path();
}

fs::path ensure(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    if (ec) log::warn("paths", "could not create {}: {}", p.string(), ec.message());
    return p;
}

// Walk up from the executable looking for the project checkout, so a build-tree
// run picks up ./models without needing MIMI_MODELS_DIR set.
fs::path find_repo_models() {
    fs::path dir = exe_dir();
    for (int depth = 0; depth < 5 && !dir.empty() && dir != dir.root_path(); ++depth) {
        std::error_code ec;
        if (fs::exists(dir / "CMakeLists.txt", ec) && fs::is_directory(dir / "models", ec)) {
            return dir / "models";
        }
        dir = dir.parent_path();
    }
    return {};
}

}  // namespace

const fs::path& exe_dir() {
    static const fs::path dir = resolve_exe_dir();
    return dir;
}

const fs::path& data_dir() {
    static const fs::path dir = [] {
        fs::path p = env_path("MIMI_DATA_DIR");
        if (p.empty()) p = home() / "Library" / "Application Support" / "Mimi";
        ensure(p);
        log::debug("paths", "data dir {}", p.string());
        return p;
    }();
    return dir;
}

const fs::path& models_dir() {
    static const fs::path dir = [] {
        fs::path p = env_path("MIMI_MODELS_DIR");
        if (p.empty()) p = find_repo_models();
        if (p.empty()) p = data_dir() / "models";
        ensure(p);
        log::debug("paths", "models dir {}", p.string());
        return p;
    }();
    return dir;
}

fs::path data_subdir(std::string_view name) {
    return ensure(data_dir() / name);
}

fs::path data_file(std::string_view name) {
    fs::path p = data_dir() / name;
    if (p.has_parent_path()) ensure(p.parent_path());
    return p;
}

fs::path find_model(std::string_view name) {
    fs::path p = models_dir() / name;
    std::error_code ec;
    return fs::exists(p, ec) ? p : fs::path{};
}

}  // namespace mimi::paths
