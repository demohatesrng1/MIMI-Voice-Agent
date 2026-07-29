#include "audio/wav.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace mimi::audio {
namespace {

std::uint32_t read_u32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t read_u16(const unsigned char* p) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}

void write_u32(std::ostream& out, std::uint32_t v) {
    const unsigned char b[4]{static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                             static_cast<unsigned char>(v >> 16),
                             static_cast<unsigned char>(v >> 24)};
    out.write(reinterpret_cast<const char*>(b), 4);
}

void write_u16(std::ostream& out, std::uint16_t v) {
    const unsigned char b[2]{static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8)};
    out.write(reinterpret_cast<const char*>(b), 2);
}

}  // namespace

WavData read_wav(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + path.string());

    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        throw std::runtime_error(path.string() + ": not a RIFF/WAVE file");
    }

    std::uint16_t channels = 0;
    std::uint16_t bits = 0;
    WavData out;

    // Walk the chunk list rather than assuming a 44-byte header: `say` emits a
    // LIST chunk before the data on some macOS versions.
    std::size_t pos = 12;
    const unsigned char* data = nullptr;
    std::size_t data_size = 0;

    while (pos + 8 <= bytes.size()) {
        const char* id = reinterpret_cast<const char*>(bytes.data() + pos);
        const std::uint32_t size = read_u32(bytes.data() + pos + 4);
        const std::size_t body = pos + 8;
        if (body + size > bytes.size()) break;

        if (std::memcmp(id, "fmt ", 4) == 0 && size >= 16) {
            const std::uint16_t format = read_u16(bytes.data() + body);
            channels = read_u16(bytes.data() + body + 2);
            out.sample_rate = read_u32(bytes.data() + body + 4);
            bits = read_u16(bytes.data() + body + 14);
            if (format != 1) {
                throw std::runtime_error(path.string() + ": only uncompressed PCM is supported");
            }
        } else if (std::memcmp(id, "data", 4) == 0) {
            data = bytes.data() + body;
            data_size = size;
        }
        pos = body + size + (size & 1);  // chunks are word-aligned
    }

    if (data == nullptr || channels == 0) {
        throw std::runtime_error(path.string() + ": missing fmt or data chunk");
    }
    if (bits != 16) {
        throw std::runtime_error(path.string() + ": need 16-bit PCM, got " +
                                 std::to_string(bits) + "-bit");
    }

    const std::size_t total = data_size / 2;
    const std::size_t frames = total / channels;
    out.samples.resize(frames);

    for (std::size_t i = 0; i < frames; ++i) {
        int sum = 0;
        for (std::uint16_t c = 0; c < channels; ++c) {
            const auto raw = static_cast<std::int16_t>(read_u16(data + (i * channels + c) * 2));
            sum += raw;
        }
        out.samples[i] = static_cast<float>(sum) / (channels * 32768.0f);
    }
    return out;
}

void write_wav(const std::filesystem::path& path, std::span<const float> samples,
               std::uint32_t sample_rate) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path.string());

    const auto data_bytes = static_cast<std::uint32_t>(samples.size() * 2);

    out.write("RIFF", 4);
    write_u32(out, 36 + data_bytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    write_u32(out, 16);
    write_u16(out, 1);  // PCM
    write_u16(out, 1);  // mono
    write_u32(out, sample_rate);
    write_u32(out, sample_rate * 2);  // byte rate
    write_u16(out, 2);                // block align
    write_u16(out, 16);               // bits
    out.write("data", 4);
    write_u32(out, data_bytes);

    for (float s : samples) {
        const float clamped = std::clamp(s, -1.0f, 1.0f);
        write_u16(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(clamped * 32767.0f)));
    }
}

}  // namespace mimi::audio
