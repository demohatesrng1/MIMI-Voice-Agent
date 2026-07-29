#pragma once

#include <onnxruntime_cxx_api.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace mimi::voice {

// One environment per process; sessions must not outlive it.
Ort::Env& ort_env();

// A loaded ONNX graph.
//
// Wraps Ort::Session mainly to resolve tensor names at load time. The
// openWakeWord classifiers were exported with whatever names the tracer
// happened to assign -- "x.1" in one, "onnx::Flatten_0" in another, numeric
// outputs like "53" and "13" -- so anything that hardcodes them works with one
// model and silently fails with the next.
class OnnxModel {
public:
    explicit OnnxModel(const std::filesystem::path& path, int intra_threads = 1);

    OnnxModel(const OnnxModel&) = delete;
    OnnxModel& operator=(const OnnxModel&) = delete;

    // Runs every input in declared order, returning every output in order.
    std::vector<Ort::Value> run(std::span<Ort::Value> inputs);

    // Convenience for the common single-in / single-out case.
    Ort::Value run_one(Ort::Value input);

    std::size_t input_count() const noexcept { return input_names_.size(); }
    std::size_t output_count() const noexcept { return output_names_.size(); }

    const std::string& input_name(std::size_t i) const { return input_names_.at(i); }
    const std::string& output_name(std::size_t i) const { return output_names_.at(i); }

    // As declared by the graph; negative entries are dynamic dimensions.
    const std::vector<std::int64_t>& input_shape(std::size_t i) const {
        return input_shapes_.at(i);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    Ort::Session session_;

    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;
    std::vector<const char*> input_ptrs_;   // stable views into the strings above
    std::vector<const char*> output_ptrs_;
    std::vector<std::vector<std::int64_t>> input_shapes_;
};

// Wraps caller-owned memory in a tensor. The data must outlive the Ort::Value
// and any Run() using it -- nothing is copied.
Ort::Value float_tensor(float* data, std::size_t count, std::span<const std::int64_t> shape);
Ort::Value int64_tensor(std::int64_t* data, std::size_t count, std::span<const std::int64_t> shape);

// Total elements implied by a shape (empty shape -> 1, a scalar).
std::size_t elements_in(std::span<const std::int64_t> shape) noexcept;

}  // namespace mimi::voice
