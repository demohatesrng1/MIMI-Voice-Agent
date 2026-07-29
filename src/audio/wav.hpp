#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace mimi::audio {

struct WavData {
    std::vector<float> samples;  // mono, normalised to +/-1.0
    std::uint32_t sample_rate = 0;
};

// Reads a 16-bit PCM RIFF/WAVE file, downmixing to mono. Throws on anything
// else -- this exists for fixtures and debug dumps, not as a general decoder.
//
// Dividing int16 by 32768 is exact in float32, so a file read here and scaled
// back up by the wake-word frontend recovers the original integers bit for bit.
// That is what makes numeric comparison against the Python reference meaningful.
WavData read_wav(const std::filesystem::path& path);

// Writes 16-bit PCM mono. For capturing what Mimi actually heard.
void write_wav(const std::filesystem::path& path, std::span<const float> samples,
               std::uint32_t sample_rate);

}  // namespace mimi::audio
