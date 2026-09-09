#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace KamataEngine {
class Audio;
}

namespace object_connect {

enum class LineHoldEndReason {
    Released,
    Cancelled,
};

// Narrow seam used by the audio lifecycle tests. Production code uses the
// KamataEngine backend selected by GameAudio's default constructor.
class GameAudioBackend {
public:
    virtual ~GameAudioBackend() = default;

    [[nodiscard]] virtual bool IsAvailable() const noexcept = 0;
    [[nodiscard]] virtual bool IsWaveAvailable(
        std::string_view relativePath) const = 0;
    virtual std::uint32_t LoadWave(std::string_view relativePath) = 0;
    virtual std::uint32_t PlayWave(std::uint32_t soundHandle, bool loop,
                                   float volume) = 0;
    virtual void StopWave(std::uint32_t voiceHandle) = 0;
    [[nodiscard]] virtual bool IsPlaying(std::uint32_t voiceHandle) = 0;
    virtual void SetVolume(std::uint32_t voiceHandle, float volume) = 0;
};

class GameAudio final {
public:
    GameAudio() noexcept = default;
    explicit GameAudio(GameAudioBackend& backend) noexcept;
    ~GameAudio();

    GameAudio(const GameAudio&) = delete;
    GameAudio& operator=(const GameAudio&) = delete;
    GameAudio(GameAudio&&) = delete;
    GameAudio& operator=(GameAudio&&) = delete;

    void Initialize(std::vector<std::string>* warnings = nullptr) noexcept;
    void Update(float deltaSeconds) noexcept;
    void RestartMusicSequence() noexcept;
    void BeginLineHold() noexcept;
    void EndLineHold(LineHoldEndReason reason) noexcept;
    void Finalize() noexcept;

private:
    enum class MusicPhase {
        Stopped,
        Intro,
        Loop,
    };

    struct Clip final {
        std::uint32_t soundHandle = 0;
        float volume = 1.0f;
        bool loaded = false;
    };

    struct Voice final {
        std::uint32_t handle = 0;
        bool active = false;
        bool observedPlaying = false;
    };

    [[nodiscard]] bool IsBackendAvailable() const noexcept;
    [[nodiscard]] bool IsWaveAvailable(std::string_view relativePath) const;
    [[nodiscard]] std::uint32_t LoadWave(std::string_view relativePath);
    [[nodiscard]] std::uint32_t PlayWave(std::uint32_t soundHandle,
                                         bool loop, float volume);
    void StopWave(std::uint32_t voiceHandle);
    [[nodiscard]] bool IsPlaying(std::uint32_t voiceHandle);
    void SetVolume(std::uint32_t voiceHandle, float volume);

    [[nodiscard]] bool StartVoice(const Clip& clip, bool loop, float volume,
                                  Voice& voice) noexcept;
    void StopVoice(Voice& voice) noexcept;
    void StartMusicSequence(std::vector<std::string>* warnings) noexcept;
    void StartLoop() noexcept;
    void UpdateMusicPhase() noexcept;
    void UpdateMusicDuck(float deltaSeconds) noexcept;
    void UpdateHoldFade(float deltaSeconds) noexcept;

    GameAudioBackend* injectedBackend_ = nullptr;
    KamataEngine::Audio* audio_ = nullptr;
    Clip intro_{};
    Clip loop_{};
    Clip hold_{};
    Clip relax_{};
    Voice musicVoice_{};
    Voice holdVoice_{};
    Voice relaxVoice_{};
    MusicPhase musicPhase_ = MusicPhase::Stopped;
    float musicDuckGain_ = 1.0f;
    float holdFadeGain_ = 1.0f;
    bool initialized_ = false;
    bool holding_ = false;
    bool musicVolumeEnabled_ = true;
    bool holdFadeActive_ = false;
};

} // namespace object_connect
