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

// Samples (-1 to 1) as a 16-bit mono WAV file, the last few milliseconds faded to silence so it
// ends without a click.
std::vector<unsigned char> wavFrom(std::vector<double> samples) {
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

std::vector<unsigned char> makePlip() {
    // 110 ms: a rising droplet, and a smaller one a major third higher just after it (which is
    // what makes it sound happy rather than like a warning).
    std::vector<double> samples(static_cast<size_t>(0.11 * kSampleRate), 0.0);
    addDroplet(samples, {0.000, 700.0, 1500.0, 0.012, 0.018, 0.30});
    addDroplet(samples, {0.040, 940.0, 1890.0, 0.010, 0.016, 0.20});
    return wavFrom(samples);
}

// A woodblock-like click: two inharmonic partials with a very fast decay, and a burst of noise
// at the start for the attack. The accent is the same click, a little louder (about 3 dB).
std::vector<unsigned char> makeTick(bool accent) {
    const double length = 0.045;
    const double pitch = 1250.0;
    const double level = accent ? 0.45 : 0.32;
    const double decay = 0.008;
    std::vector<double> samples(static_cast<size_t>(length * kSampleRate), 0.0);
    uint32_t noise = 12345u;
    for (size_t i = 0; i < samples.size(); ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        const double body = std::sin(kTwoPi * pitch * t) + 0.45 * std::sin(kTwoPi * pitch * 2.76 * t);
        noise = noise * 1664525u + 1013904223u;
        const double hiss = (static_cast<double>(noise >> 8) / 8388608.0 - 1.0) * std::exp(-t / 0.0015);
        const double attack = std::min(1.0, t / 0.0005);
        samples[i] = level * attack * (std::exp(-t / decay) * body / 1.45 + 0.35 * hiss);
    }
    return wavFrom(samples);
}

}  // namespace

const std::vector<unsigned char>& plipWav() {
    static const std::vector<unsigned char> wav = makePlip();
    return wav;
}

const std::vector<unsigned char>& tickWav(bool accent) {
    static const std::vector<unsigned char> tick = makeTick(false);
    static const std::vector<unsigned char> accented = makeTick(true);
    return accent ? accented : tick;
}
