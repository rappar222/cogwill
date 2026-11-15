#pragma once

#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace cogwill {

enum class SfxType : int {
    Place = 0,
    Remove,
    Upgrade,
    Crank,
    Error,
    Milestone,
    Count
};

class AudioSystem {
public:
    AudioSystem();

    void play(SfxType type, float volume = 45.0f, float pitch = 1.0f);
    void setMuted(bool muted) { muted_ = muted; }
    bool isMuted() const { return muted_; }
    void toggleMute() { muted_ = !muted_; }

    void setMasterVolume(float vol01) { masterVolume_ = std::clamp(vol01, 0.0f, 1.0f); }
    float masterVolume() const { return masterVolume_; }

private:
    void buildBuffer(SfxType type, const std::vector<std::int16_t>& samples);

    std::array<sf::SoundBuffer, static_cast<std::size_t>(SfxType::Count)> buffers_;
    std::vector<sf::Sound> voices_;
    bool muted_{false};
    float masterVolume_{0.80f};
};

} // namespace cogwill
