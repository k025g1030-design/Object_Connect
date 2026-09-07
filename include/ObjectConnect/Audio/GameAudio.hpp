#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace KamataEngine {
class Audio;
}

namespace object_connect {

class GameAudio final {
public:
    GameAudio() noexcept = default;
    ~GameAudio();

    GameAudio(const GameAudio&) = delete;
    GameAudio& operator=(const GameAudio&) = delete;
    GameAudio(GameAudio&&) = delete;
    GameAudio& operator=(GameAudio&&) = delete;

    void Initialize(std::vector<std::string>* warnings = nullptr) noexcept;
    void PlayLevelSelected() noexcept;
    void PlayNodeSelected() noexcept;
    void Finalize() noexcept;

private:
    struct Clip final {
        std::uint32_t soundHandle = 0;
        float volume = 1.0f;
        bool loaded = false;
    };

    void PlayOneShot(const Clip& clip) noexcept;

    KamataEngine::Audio* audio_ = nullptr;
    Clip bgm_{};
    Clip levelSelected_{};
    Clip nodeSelected_{};
    std::uint32_t bgmVoiceHandle_ = 0;
    bool bgmPlaying_ = false;
};

} // namespace object_connect
