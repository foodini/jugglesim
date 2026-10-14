// sound.cpp - see sound.h.
#include "sound.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

constexpr int kSampleRate = 44100;
constexpr double kTwoPi = 6.283185307179586;

// One droplet: a sine whose pitch rises quickly (from `from` to `to` Hz, most of the rise in the
// first `rise` seconds), starting after `delay` seconds, with a 2 ms attack and an exponential
// decay (`decay`: seconds to fall to 1/e), at `level` (0-1).
struct Droplet {
    double delay, from, to, rise, decay, level;
};

void addDroplet(std::vector<double>& out, const Droplet& d) {
    double phase = 0.0;
    const size_t first = static_cast<size_t>(d.delay * kSampleRate);
    for (size_t i = first; i < out.size(); ++i) {
        const double t = static_cast<double>(i - first) / kSampleRate;
        const double frequency = d.to + (d.from - d.to) * std::exp(-t / d.rise);
        phase += kTwoPi * frequency / kSampleRate;
        const double attack = std::min(1.0, t / 0.002);
        out[i] += d.level * attack * std::exp(-t / d.decay) * std::sin(phase);
    }
}

void put16(std::vector<unsigned char>& out, uint16_t v) {
    out.push_back(static_cast<unsigned char>(v & 0xff));
    out.push_back(static_cast<unsigned char>(v >> 8));
}
void put32(std::vector<unsigned char>& out, uint32_t v) {
    put16(out, static_cast<uint16_t>(v & 0xffff));
    put16(out, static_cast<uint16_t>(v >> 16));
}
void putTag(std::vector<unsigned char>& out, const char* tag) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(tag[i]));
}

std::vector<unsigned char> makePlip() {
    // 110 ms: a rising droplet, and a smaller one a major third higher just after it (which is
    // what makes it sound happy rather than like a warning).
    std::vector<double> samples(static_cast<size_t>(0.11 * kSampleRate), 0.0);
    addDroplet(samples, {0.000, 700.0, 1500.0, 0.012, 0.018, 0.30});
    addDroplet(samples, {0.040, 940.0, 1890.0, 0.010, 0.016, 0.20});
    // The last few milliseconds fade to silence, so it ends without a click.
    const size_t fade = static_cast<size_t>(0.005 * kSampleRate);
    for (size_t i = 0; i < fade && i < samples.size(); ++i) samples[samples.size() - 1 - i] *= static_cast<double>(i) / fade;

    std::vector<unsigned char> wav;
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
    putTag(wav, "RIFF");
    put32(wav, 36 + dataBytes);
    putTag(wav, "WAVE");
    putTag(wav, "fmt ");
    put32(wav, 16);               // the format chunk's size
    put16(wav, 1);                // PCM
    put16(wav, 1);                // mono
    put32(wav, kSampleRate);
    put32(wav, kSampleRate * 2);  // bytes per second
    put16(wav, 2);                // bytes per sample
    put16(wav, 16);               // bits per sample
    putTag(wav, "data");
    put32(wav, dataBytes);
    for (const double s : samples) {
        const double clamped = std::max(-1.0, std::min(1.0, s));
        put16(wav, static_cast<uint16_t>(static_cast<int16_t>(std::lround(clamped * 32767.0))));
    }
    return wav;
}

}  // namespace

const std::vector<unsigned char>& plipWav() {
    static const std::vector<unsigned char> wav = makePlip();
    return wav;
}
