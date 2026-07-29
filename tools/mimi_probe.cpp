// mimi_probe -- dump the input/output signature of an ONNX model.
//
//   mimi_probe models/silero_vad.onnx models/melspectrogram.onnx ...
//
// Exists so the VAD and wake-word wrappers are written against the graphs that
// actually shipped, rather than against what the docs claim they contain.

#include <onnxruntime_cxx_api.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* type_name(ONNXTensorElementDataType t) {
    switch (t) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:  return "float32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:  return "uint8";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:   return "int8";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:  return "int16";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:  return "int32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:  return "int64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE: return "float64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:   return "bool";
        default:                                   return "other";
    }
}

std::string shape_of(const std::vector<std::int64_t>& dims) {
    std::string out = "[";
    for (std::size_t i = 0; i < dims.size(); ++i) {
        if (i) out += ", ";
        out += dims[i] < 0 ? std::string("?") : std::to_string(dims[i]);
    }
    return out + "]";
}

void describe(Ort::Session& session, bool inputs) {
    Ort::AllocatorWithDefaultOptions alloc;
    const std::size_t count = inputs ? session.GetInputCount() : session.GetOutputCount();
    std::printf("  %s (%zu)\n", inputs ? "inputs" : "outputs", count);

    for (std::size_t i = 0; i < count; ++i) {
        auto name = inputs ? session.GetInputNameAllocated(i, alloc)
                           : session.GetOutputNameAllocated(i, alloc);
        auto info = inputs ? session.GetInputTypeInfo(i) : session.GetOutputTypeInfo(i);
        const auto tensor = info.GetTensorTypeAndShapeInfo();
        std::printf("    %-24s %-8s %s\n", name.get(), type_name(tensor.GetElementType()),
                    shape_of(tensor.GetShape()).c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fputs("usage: mimi_probe MODEL.onnx [MODEL.onnx ...]\n", stderr);
        return 2;
    }

    Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "mimi_probe");
    int failures = 0;

    for (int i = 1; i < argc; ++i) {
        std::printf("\n%s\n", argv[i]);
        try {
            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(1);
            Ort::Session session(env, argv[i], options);
            describe(session, true);
            describe(session, false);
        } catch (const Ort::Exception& e) {
            std::printf("  ERROR: %s\n", e.what());
            ++failures;
        }
    }
    std::printf("\n");
    return failures == 0 ? 0 : 1;
}
