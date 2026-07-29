#pragma once

#include <filesystem>
#include <string_view>

namespace mimi::paths {

// Where Mimi keeps everything she writes: journal/, digests/, notes/, pages/,
// profile.md, config.json.  ~/Library/Application Support/Mimi, overridable
// with MIMI_DATA_DIR.  Created on first call.
const std::filesystem::path& data_dir();

// Where the ONNX / GGML model files live.  MIMI_MODELS_DIR, else <repo>/models
// when running from a build tree, else <data_dir>/models.  Created on first call.
const std::filesystem::path& models_dir();

// Directory containing the running executable.
const std::filesystem::path& exe_dir();

// data_dir() / name, with parent directories created.
std::filesystem::path data_file(std::string_view name);

// A subdirectory of data_dir(), created if missing.
std::filesystem::path data_subdir(std::string_view name);

// Looks for `name` in models_dir().  Returns an empty path if it isn't there,
// so callers can offer a download instead of throwing.
std::filesystem::path find_model(std::string_view name);

}  // namespace mimi::paths
