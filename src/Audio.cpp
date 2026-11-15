#include "Audio.hpp"

#include "Types.hpp"

#include <SFML/Audio/SoundChannel.hpp>
#include <algorithm>
#include <cmath>

namespace cogwill {

namespace {

constexpr unsigned int SAMPLE_RATE = 44100;

std::vector<std::int16_t> synthTone(
    float durationSec,
    float fStart,
    float fEnd,
    float decay,
    float noiseMix,
    float amp
) {
    std::size_t count = static_cast<std::size_t>(durationSec * SAMPLE_RATE);
    std::vector<std::int16_t> samples(count, 0);
    float phase = 0.0f;
    uint32_t rng = 0x1234567u;
    for (std::size_t i = 0; i < count; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
        float u = static_cast<float>(i) / static_cast<float>(std::max<std::size_t>(1, count));
        float freq = fStart + (fEnd - fStart) * u;
        phase += TWO_PI * freq / static_cast<float>(SAMPLE_RATE);
        float env = std::exp(-decay * t) * std::min(1.0f, t / 0.004f);

        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        float noise = (static_cast<float>(rng & 0xFFFFu) / 32768.0f) - 1.0f;
        float wave = std::sin(phase) * (1.0f - noiseMix) + noise * noiseMix;
        float val = std::clamp(wave * env * amp, -1.0f, 1.0f);
        samples[i] = static_cast<std::int16_t>(val * 30000.0f);
    }
    return samples;
}

std::vector<std::int16_t> synthChime() {
    float durationSec = 0.34f;
    std::size_t count = static_cast<std::size_t>(durationSec * SAMPLE_RATE);
    std::vector<std::int16_t> samples(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
        float env1 = std::exp(-9.0f * t) * std::min(1.0f, t / 0.005f);
        float t2 = std::max(0.0f, t - 0.07f);
        float env2 = (t >= 0.07f) ? std::exp(-8.5f * t2) * std::min(1.0f, t2 / 0.005f) : 0.0f;
        float w = 0.55f * std::sin(TWO_PI * 523.25f * t) * env1
                + 0.65f * std::sin(TWO_PI * 783.99f * t2) * env2;
        samples[i] = static_cast<std::int16_t>(std::clamp(w * 0.45f, -1.0f, 1.0f) * 30000.0f);
    }
    return samples;
}

std::vector<std::int16_t> synthFanfare() {
    float durationSec = 0.68f;
    std::size_t count = static_cast<std::size_t>(durationSec * SAMPLE_RATE);
    std::vector<std::int16_t> samples(count, 0);
    constexpr float notes[4] = {523.25f, 659.25f, 783.99f, 1046.50f};
    constexpr float starts[4] = {0.0f, 0.09f, 0.18f, 0.28f};
    for (std::size_t i = 0; i < count; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
        float w = 0.0f;
        for (int n = 0; n < 4; ++n) {
            if (t >= starts[n]) {
                float tn = t - starts[n];
                float env = std::exp(-5.8f * tn) * std::min(1.0f, tn / 0.006f);
                w += 0.36f * std::sin(TWO_PI * notes[n] * tn) * env;
            }
        }
        samples[i] = static_cast<std::int16_t>(std::clamp(w, -1.0f, 1.0f) * 30000.0f);
    }
    return samples;
}

} // namespace

AudioSystem::AudioSystem() {
    buildBuffer(SfxType::Place, synthTone(0.09f, 190.0f, 105.0f, 34.0f, 0.22f, 0.45f));
    buildBuffer(SfxType::Remove, synthTone(0.08f, 260.0f, 140.0f, 38.0f, 0.12f, 0.36f));
    buildBuffer(SfxType::Upgrade, synthChime());
    buildBuffer(SfxType::Crank, synthTone(0.045f, 340.0f, 220.0f, 65.0f, 0.30f, 0.32f));
    buildBuffer(SfxType::Error, synthTone(0.14f, 120.0f, 82.0f, 18.0f, 0.15f, 0.42f));
    buildBuffer(SfxType::Milestone, synthFanfare());
}

void AudioSystem::buildBuffer(SfxType type, const std::vector<std::int16_t>& samples) {
    (void)buffers_[static_cast<std::size_t>(type)].loadFromSamples(
        samples.data(),
        static_cast<std::uint64_t>(samples.size()),
        1,
        SAMPLE_RATE,
        {sf::SoundChannel::Mono}
    );
}

void AudioSystem::play(SfxType type, float volume, float pitch) {
    if (muted_ || masterVolume_ <= 0.005f) {
        return;
    }
    const sf::SoundBuffer& buf = buffers_[static_cast<std::size_t>(type)];
    if (buf.getSampleCount() == 0) {
        return;
    }

    voices_.erase(
        std::remove_if(voices_.begin(), voices_.end(), [](const sf::Sound& s) {
            return s.getStatus() == sf::Sound::Status::Stopped;
        }),
        voices_.end()
    );

    if (voices_.size() >= 16) {
        voices_.erase(voices_.begin());
    }

    voices_.emplace_back(buf);
    sf::Sound& snd = voices_.back();
    snd.setVolume(std::clamp(volume * masterVolume_, 0.0f, 100.0f));
    snd.setPitch(pitch);
    snd.play();
}

} // namespace cogwill
