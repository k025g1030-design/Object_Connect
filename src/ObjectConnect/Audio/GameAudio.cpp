#include "ObjectConnect/Audio/GameAudio.hpp"

#include <audio/Audio.h>

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

// TODO(audio-assets): Add the final looping BGM WAV at audio/bgm.wav.
// TODO(audio-mix): Tune the BGM volume after the final asset is mastered.
constexpr AudioAsset kBgmAsset{"audio/bgm.wav", 1.0f};

// TODO(audio-assets): Add the final level-selection WAV at audio/level_select.wav.
// TODO(audio-mix): Tune the level-selection volume with the final UI sound.
constexpr AudioAsset kLevelSelectedAsset{"audio/level_select.wav", 1.0f};

// TODO(audio-assets): Add the final node-selection WAV at audio/node_select.wav.
// TODO(audio-mix): Tune the node-selection volume with the final gameplay sound.
constexpr AudioAsset kNodeSelectedAsset{"audio/node_select.wav", 1.0f};

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

} // namespace

GameAudio::~GameAudio() { Finalize(); }

void GameAudio::Initialize(
    std::vector<std::string>* const warnings) noexcept {
    Finalize();

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

    const auto loadOptional =
        [this, warnings](const AudioAsset asset, Clip& clip) noexcept {
            clip.volume = asset.volume;
            try {
                const std::filesystem::path fullPath =
                    std::filesystem::path{kEngineResourceRoot} /
                    std::filesystem::path{asset.relativePath};
                std::error_code statusError;
                const bool present =
                    std::filesystem::is_regular_file(fullPath, statusError);
                if (statusError) {
                    AppendWarning(warnings, asset.relativePath,
                                  "could not be inspected; this channel is disabled.");
                    return;
                }
                if (!present) {
                    return;
                }
                clip.soundHandle =
                    audio_->LoadWave(std::string{asset.relativePath});
                clip.loaded = true;
            } catch (const std::exception& exception) {
                AppendWarning(warnings, asset.relativePath, exception.what());
            } catch (...) {
                AppendWarning(warnings, asset.relativePath,
                              "could not be loaded; this channel is disabled.");
            }
        };

    loadOptional(kBgmAsset, bgm_);
    loadOptional(kLevelSelectedAsset, levelSelected_);
    loadOptional(kNodeSelectedAsset, nodeSelected_);

    if (bgm_.loaded) {
        try {
            bgmVoiceHandle_ =
                audio_->PlayWave(bgm_.soundHandle, true, bgm_.volume);
            bgmPlaying_ = true;
        } catch (const std::exception& exception) {
            AppendWarning(warnings, kBgmAsset.relativePath, exception.what());
            bgm_.loaded = false;
        } catch (...) {
            AppendWarning(warnings, kBgmAsset.relativePath,
                          "could not be started; this channel is disabled.");
            bgm_.loaded = false;
        }
    }
}

void GameAudio::PlayLevelSelected() noexcept {
    PlayOneShot(levelSelected_);
}

void GameAudio::PlayNodeSelected() noexcept {
    PlayOneShot(nodeSelected_);
}

void GameAudio::Finalize() noexcept {
    KamataEngine::Audio* const audio = audio_;
    const std::uint32_t bgmVoiceHandle = bgmVoiceHandle_;
    const bool stopBgm = bgmPlaying_;

    audio_ = nullptr;
    bgm_ = {};
    levelSelected_ = {};
    nodeSelected_ = {};
    bgmVoiceHandle_ = 0;
    bgmPlaying_ = false;

    if (audio != nullptr && stopBgm) {
        try {
            audio->StopWave(bgmVoiceHandle);
        } catch (...) {
            // Finalization is best-effort and must remain noexcept.
        }
    }
}

void GameAudio::PlayOneShot(const Clip& clip) noexcept {
    if (audio_ == nullptr || !clip.loaded) {
        return;
    }
    try {
        static_cast<void>(
            audio_->PlayWave(clip.soundHandle, false, clip.volume));
    } catch (...) {
        // Runtime sound effects are optional and never affect input handling.
    }
}

} // namespace object_connect
