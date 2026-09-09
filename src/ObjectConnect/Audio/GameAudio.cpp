#include "ObjectConnect/Audio/GameAudio.hpp"

#include <audio/Audio.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace object_connect {
namespace {

struct AudioAsset final {
    std::string_view relativePath;
    float volume = 1.0f;
};

constexpr AudioAsset kIntroAsset{"audio/bgm_start.wav", 0.9f};
constexpr AudioAsset kLoopAsset{"audio/bgm_loop.wav", 0.4f};
constexpr AudioAsset kHoldAsset{"audio/line_hold.wav", 0.65f};
constexpr AudioAsset kRelaxAsset{"audio/line_relax.wav", 2.0f};

constexpr float kDuckGain = 0.4f;
constexpr float kDuckAttackSeconds = 0.05f;
constexpr float kDuckReleaseSeconds = 0.25f;
constexpr float kHoldFadeSeconds = 0.05f;

// KamataEngine initializes Audio with this process-relative resource root.
// The existence check and LoadWave must resolve to this same location.
constexpr std::string_view kEngineResourceRoot = "Resources";

void AppendWarning(std::vector<std::string>* const warnings,
                   const std::string_view relativePath,
                   const std::string_view detail) noexcept {
    if (warnings == nullptr) {
        return;
    }
    try {
        std::string warning{"Optional audio asset '"};
        warning += relativePath;
        warning += "' ";
        warning += detail;
        warnings->push_back(std::move(warning));
    } catch (...) {
        // Diagnostics are optional and must not make optional audio fatal.
    }
}

[[nodiscard]] float NormalizeDelta(const float deltaSeconds) noexcept {
    return std::isfinite(deltaSeconds) && deltaSeconds > 0.0f
               ? deltaSeconds
               : 0.0f;
}

[[nodiscard]] float MoveTowards(const float value, const float target,
                                const float maximumDelta) noexcept {
    if (value < target) {
        return std::min(value + maximumDelta, target);
    }
    return std::max(value - maximumDelta, target);
}

} // namespace

GameAudio::GameAudio(GameAudioBackend& backend) noexcept
    : injectedBackend_(&backend) {}

GameAudio::~GameAudio() { Finalize(); }

void GameAudio::Initialize(
    std::vector<std::string>* const warnings) noexcept {
    Finalize();

    if (injectedBackend_ != nullptr) {
        if (!injectedBackend_->IsAvailable()) {
            AppendWarning(warnings, "audio backend",
                          "is unavailable; all audio is disabled.");
            return;
        }
    } else {
        try {
            audio_ = KamataEngine::Audio::GetInstance();
        } catch (const std::exception& exception) {
            AppendWarning(warnings, "KamataEngine Audio", exception.what());
            return;
        } catch (...) {
            AppendWarning(warnings, "KamataEngine Audio",
                          "could not be acquired; all audio is disabled.");
            return;
        }
        if (audio_ == nullptr) {
            AppendWarning(warnings, "KamataEngine Audio",
                          "is unavailable; all audio is disabled.");
            return;
        }
    }

    initialized_ = true;
    const auto loadOptional =
        [this, warnings](const AudioAsset asset, Clip& clip) noexcept {
            clip.volume = asset.volume;
            try {
                if (!IsWaveAvailable(asset.relativePath)) {
                    return;
                }
                clip.soundHandle = LoadWave(asset.relativePath);
                // Zero is a valid KamataEngine sound handle. Loaded state must
                // therefore be tracked independently from the numeric value.
                clip.loaded = true;
            } catch (const std::exception& exception) {
                AppendWarning(warnings, asset.relativePath, exception.what());
            } catch (...) {
                AppendWarning(warnings, asset.relativePath,
                              "could not be loaded; this channel is disabled.");
            }
        };

    loadOptional(kIntroAsset, intro_);
    loadOptional(kLoopAsset, loop_);
    loadOptional(kHoldAsset, hold_);
    loadOptional(kRelaxAsset, relax_);
    StartMusicSequence(warnings);
}

void GameAudio::Update(const float deltaSeconds) noexcept {
    if (!initialized_ || !IsBackendAvailable()) {
        return;
    }

    UpdateMusicPhase();
    const float normalizedDelta = NormalizeDelta(deltaSeconds);
    UpdateMusicDuck(normalizedDelta);
    UpdateHoldFade(normalizedDelta);
}

void GameAudio::RestartMusicSequence() noexcept {
    if (!initialized_ || !IsBackendAvailable()) {
        return;
    }
    StopVoice(musicVoice_);
    musicPhase_ = MusicPhase::Stopped;
    StartMusicSequence(nullptr);
}

void GameAudio::BeginLineHold() noexcept {
    if (!initialized_ || !IsBackendAvailable() || holding_) {
        return;
    }

    holding_ = true;
    holdFadeActive_ = false;
    holdFadeGain_ = 1.0f;
    StopVoice(holdVoice_);
    static_cast<void>(StartVoice(hold_, false, hold_.volume, holdVoice_));
}

void GameAudio::EndLineHold(const LineHoldEndReason reason) noexcept {
    if (!holding_) {
        return;
    }

    holding_ = false;
    holdFadeGain_ = 1.0f;
    holdFadeActive_ = holdVoice_.active;

    if (reason == LineHoldEndReason::Released && initialized_ &&
        IsBackendAvailable()) {
        StopVoice(relaxVoice_);
        static_cast<void>(
            StartVoice(relax_, false, relax_.volume, relaxVoice_));
    }
}

void GameAudio::Finalize() noexcept {
    // Clear the logical state before making best-effort backend calls so a
    // throwing backend cannot make a second Finalize repeat the operation.
    holding_ = false;
    holdFadeActive_ = false;
    musicPhase_ = MusicPhase::Stopped;
    StopVoice(musicVoice_);
    StopVoice(holdVoice_);
    StopVoice(relaxVoice_);

    intro_ = {};
    loop_ = {};
    hold_ = {};
    relax_ = {};
    musicDuckGain_ = 1.0f;
    holdFadeGain_ = 1.0f;
    musicVolumeEnabled_ = true;
    initialized_ = false;
    audio_ = nullptr;
}

bool GameAudio::IsBackendAvailable() const noexcept {
    return injectedBackend_ != nullptr ? injectedBackend_->IsAvailable()
                                       : audio_ != nullptr;
}

bool GameAudio::IsWaveAvailable(
    const std::string_view relativePath) const {
    if (injectedBackend_ != nullptr) {
        return injectedBackend_->IsWaveAvailable(relativePath);
    }

    const std::filesystem::path fullPath =
        std::filesystem::path{kEngineResourceRoot} /
        std::filesystem::path{relativePath};
    std::error_code statusError;
    const bool present =
        std::filesystem::is_regular_file(fullPath, statusError);
    if (statusError) {
        throw std::filesystem::filesystem_error{
            "could not inspect optional audio asset", fullPath, statusError};
    }
    return present;
}

std::uint32_t GameAudio::LoadWave(const std::string_view relativePath) {
    if (injectedBackend_ != nullptr) {
        return injectedBackend_->LoadWave(relativePath);
    }
    return audio_->LoadWave(std::string{relativePath});
}

std::uint32_t GameAudio::PlayWave(const std::uint32_t soundHandle,
                                  const bool loop, const float volume) {
    if (injectedBackend_ != nullptr) {
        return injectedBackend_->PlayWave(soundHandle, loop, volume);
    }
    return audio_->PlayWave(soundHandle, loop, volume);
}

void GameAudio::StopWave(const std::uint32_t voiceHandle) {
    if (injectedBackend_ != nullptr) {
        injectedBackend_->StopWave(voiceHandle);
        return;
    }
    audio_->StopWave(voiceHandle);
}

bool GameAudio::IsPlaying(const std::uint32_t voiceHandle) {
    if (injectedBackend_ != nullptr) {
        return injectedBackend_->IsPlaying(voiceHandle);
    }
    return audio_->IsPlaying(voiceHandle);
}

void GameAudio::SetVolume(const std::uint32_t voiceHandle,
                          const float volume) {
    if (injectedBackend_ != nullptr) {
        injectedBackend_->SetVolume(voiceHandle, volume);
        return;
    }
    audio_->SetVolume(voiceHandle, volume);
}

bool GameAudio::StartVoice(const Clip& clip, const bool loop,
                           const float volume, Voice& voice) noexcept {
    voice = {};
    if (!clip.loaded || !IsBackendAvailable()) {
        return false;
    }
    try {
        voice.handle = PlayWave(clip.soundHandle, loop, volume);
        // As with sound handles, voice handle zero is valid.
        voice.active = true;
        return true;
    } catch (...) {
        voice = {};
        return false;
    }
}

void GameAudio::StopVoice(Voice& voice) noexcept {
    if (!voice.active) {
        return;
    }
    const std::uint32_t handle = voice.handle;
    voice = {};
    if (!IsBackendAvailable()) {
        return;
    }
    try {
        StopWave(handle);
    } catch (...) {
        // Runtime audio is optional and finalization must remain noexcept.
    }
}

void GameAudio::StartMusicSequence(
    std::vector<std::string>* const warnings) noexcept {
    musicVolumeEnabled_ = true;
    if (intro_.loaded) {
        if (StartVoice(intro_, false, intro_.volume * musicDuckGain_,
                       musicVoice_)) {
            musicPhase_ = MusicPhase::Intro;
            return;
        }
        AppendWarning(warnings, kIntroAsset.relativePath,
                      "could not be started; continuing without the intro.");
    }

    if (loop_.loaded) {
        if (StartVoice(loop_, true, loop_.volume * musicDuckGain_,
                       musicVoice_)) {
            musicPhase_ = MusicPhase::Loop;
            return;
        }
        AppendWarning(warnings, kLoopAsset.relativePath,
                      "could not be started; music is disabled.");
    }
    musicPhase_ = MusicPhase::Stopped;
}

void GameAudio::StartLoop() noexcept {
    musicVoice_ = {};
    musicVolumeEnabled_ = true;
    if (StartVoice(loop_, true, loop_.volume * musicDuckGain_, musicVoice_)) {
        musicPhase_ = MusicPhase::Loop;
    } else {
        // Stopped prevents a failed loop start from being retried each frame.
        musicPhase_ = MusicPhase::Stopped;
    }
}

void GameAudio::UpdateMusicPhase() noexcept {
    if (musicPhase_ != MusicPhase::Intro || !musicVoice_.active) {
        return;
    }

    bool playing = false;
    try {
        playing = IsPlaying(musicVoice_.handle);
    } catch (...) {
        StopVoice(musicVoice_);
        musicPhase_ = MusicPhase::Stopped;
        return;
    }
    if (playing) {
        musicVoice_.observedPlaying = true;
        return;
    }

    // KamataEngine::IsPlaying currently reports SamplesPlayed != 0. A voice
    // can therefore return false immediately after PlayWave, before the audio
    // render thread has produced its first sample. Only a true -> false
    // transition proves that the intro actually reached its end.
    if (!musicVoice_.observedPlaying) {
        return;
    }

    // StopWave is harmless when the backend already retired the completed
    // one-shot and prevents an incorrectly retained voice from becoming an
    // untracked source before the loop starts.
    StopVoice(musicVoice_);
    StartLoop();
}

void GameAudio::UpdateMusicDuck(const float deltaSeconds) noexcept {
    const float targetGain = holding_ ? kDuckGain : 1.0f;
    const float duration = holding_ ? kDuckAttackSeconds
                                    : kDuckReleaseSeconds;
    const float maximumDelta =
        duration > 0.0f ? ((1.0f - kDuckGain) / duration) * deltaSeconds
                        : (1.0f - kDuckGain);
    const float nextGain =
        MoveTowards(musicDuckGain_, targetGain, maximumDelta);
    if (nextGain == musicDuckGain_) {
        return;
    }
    musicDuckGain_ = nextGain;

    if (!musicVoice_.active || !musicVolumeEnabled_) {
        return;
    }
    const float baseVolume = musicPhase_ == MusicPhase::Intro
                                 ? intro_.volume
                                 : loop_.volume;
    try {
        SetVolume(musicVoice_.handle, baseVolume * musicDuckGain_);
    } catch (...) {
        // A persistent SetVolume failure must not be retried every frame.
        musicVolumeEnabled_ = false;
    }
}

void GameAudio::UpdateHoldFade(const float deltaSeconds) noexcept {
    if (!holdFadeActive_ || !holdVoice_.active) {
        return;
    }

    const float fadeStep =
        kHoldFadeSeconds > 0.0f ? deltaSeconds / kHoldFadeSeconds : 1.0f;
    holdFadeGain_ = std::max(0.0f, holdFadeGain_ - fadeStep);
    if (holdFadeGain_ <= 0.0f) {
        holdFadeActive_ = false;
        StopVoice(holdVoice_);
        return;
    }

    try {
        SetVolume(holdVoice_.handle, hold_.volume * holdFadeGain_);
    } catch (...) {
        holdFadeActive_ = false;
        StopVoice(holdVoice_);
    }
}

} // namespace object_connect
