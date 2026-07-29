#include "voice/onnx_model.hpp"

#include "core/log.hpp"

#include <numeric>
#include <stdexcept>

namespace mimi::voice {
namespace {
constexpr std::string_view kTag = "onnx";

Ort::MemoryInfo& cpu_memory() {
    static Ort::MemoryInfo info =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    return info;
}
}  // namespace

Ort::Env& ort_env() {
    static Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "mimi");
    return env;
}

std::size_t elements_in(std::span<const std::int64_t> shape) noexcept {
    return std::accumulate(shape.begin(), shape.end(), std::size_t{1},
                           [](std::size_t acc, std::int64_t d) {
                               return d > 0 ? acc * static_cast<std::size_t>(d) : acc;
                           });
}

Ort::Value float_tensor(float* data, std::size_t count, std::span<const std::int64_t> shape) {
    return Ort::Value::CreateTensor<float>(cpu_memory(), data, count, shape.data(), shape.size());
}

Ort::Value int64_tensor(std::int64_t* data, std::size_t count,
                        std::span<const std::int64_t> shape) {
    return Ort::Value::CreateTensor<std::int64_t>(cpu_memory(), data, count, shape.data(),
                                                  shape.size());
}

namespace {

Ort::SessionOptions make_options(int intra_threads) {
    Ort::SessionOptions options;
    // These graphs are tiny and run every 32-80 ms. Extra threads cost more in
    // scheduling and wakeups than they save, and they fight the audio thread
    // for cores, so keep inference single-threaded and sequential.
    options.SetIntraOpNumThreads(intra_threads);
    options.SetInterOpNumThreads(1);
    options.SetExecutionMode(ORT_SEQUENTIAL);
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    return options;
}

}  // namespace

OnnxModel::OnnxModel(const std::filesystem::path& path, int intra_threads)
    : path_(path),
      session_(nullptr) {
    if (path_.empty()) {
        throw std::runtime_error("no model path given");
    }
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) {
        throw std::runtime_error("model not found: " + path_.string() +
                                 "  (run scripts/fetch_models.sh)");
    }

    try {
        session_ = Ort::Session(ort_env(), path_.c_str(), make_options(intra_threads));
    } catch (const Ort::Exception& e) {
        throw std::runtime_error("could not load " + path_.string() + ": " + e.what());
    }

    Ort::AllocatorWithDefaultOptions alloc;

    input_names_.reserve(session_.GetInputCount());
    input_shapes_.reserve(session_.GetInputCount());
    for (std::size_t i = 0; i < session_.GetInputCount(); ++i) {
        input_names_.emplace_back(session_.GetInputNameAllocated(i, alloc).get());
        input_shapes_.emplace_back(
            session_.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape());
    }
    output_names_.reserve(session_.GetOutputCount());
    for (std::size_t i = 0; i < session_.GetOutputCount(); ++i) {
        output_names_.emplace_back(session_.GetOutputNameAllocated(i, alloc).get());
    }

    // Built once, after the name vectors have stopped growing, so the pointers
    // stay valid for the life of the model.
    input_ptrs_.reserve(input_names_.size());
    for (const auto& n : input_names_) input_ptrs_.push_back(n.c_str());
    output_ptrs_.reserve(output_names_.size());
    for (const auto& n : output_names_) output_ptrs_.push_back(n.c_str());

    log::debug(kTag, "loaded {} ({} in, {} out)", path_.filename().string(),
               input_names_.size(), output_names_.size());
}

std::vector<Ort::Value> OnnxModel::run(std::span<Ort::Value> inputs) {
    if (inputs.size() != input_ptrs_.size()) {
        throw std::runtime_error(path_.filename().string() + ": expected " +
                                 std::to_string(input_ptrs_.size()) + " inputs, got " +
                                 std::to_string(inputs.size()));
    }
    return session_.Run(Ort::RunOptions{nullptr}, input_ptrs_.data(), inputs.data(),
                        inputs.size(), output_ptrs_.data(), output_ptrs_.size());
}

Ort::Value OnnxModel::run_one(Ort::Value input) {
    auto outputs = run(std::span<Ort::Value>(&input, 1));
    if (outputs.empty()) {
        throw std::runtime_error(path_.filename().string() + ": produced no output");
    }
    return std::move(outputs.front());
}

}  // namespace mimi::voice
