#include "../TestSupport.hpp"

#include "ObjectConnect/Audio/GameAudio.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace object_connect::tests {
namespace {

struct PlayCall final {
    std::uint32_t soundHandle = 0;
    std::uint32_t voiceHandle = 0;
    bool loop = false;
    float volume = 0.0f;
};

struct VolumeCall final {
    std::uint32_t voiceHandle = 0;
    float volume = 0.0f;
};

class FakeAudioBackend final : public GameAudioBackend {
public:
    FakeAudioBackend() {
        availableWaves = {
            "audio/bgm_start.wav",
            "audio/bgm_loop.wav",
            "audio/line_hold.wav",
            "audio/line_relax.wav",
        };
    }

    [[nodiscard]] bool IsAvailable() const noexcept override {
        return available;
    }

    [[nodiscard]] bool IsWaveAvailable(
        const std::string_view relativePath) const override {
        return availableWaves.contains(std::string{relativePath});
    }

    std::uint32_t LoadWave(const std::string_view relativePath) override {
        const std::uint32_t handle = nextSoundHandle++;
        loadedPaths.emplace_back(relativePath);
        soundHandles.emplace(std::string{relativePath}, handle);
        return handle;
    }

    std::uint32_t PlayWave(const std::uint32_t soundHandle, const bool loop,
                           const float volume) override {
        if (throwOnPlaySoundHandle == soundHandle) {
            throw std::runtime_error{"play failure"};
        }
        const std::uint32_t voiceHandle = nextVoiceHandle++;
        playCalls.push_back({soundHandle, voiceHandle, loop, volume});
        playing[voiceHandle] = true;
        return voiceHandle;
    }

    void StopWave(const std::uint32_t voiceHandle) override {
        stopCalls.push_back(voiceHandle);
        playing[voiceHandle] = false;
    }

    [[nodiscard]] bool IsPlaying(
        const std::uint32_t voiceHandle) override {
        ++isPlayingCalls[voiceHandle];
        if (throwOnIsPlayingVoice == voiceHandle) {
            throw std::runtime_error{"poll failure"};
        }
        if (const auto scripted = scriptedPlaying.find(voiceHandle);
            scripted != scriptedPlaying.end() && !scripted->second.empty()) {
            const bool result = scripted->second.front();
            scripted->second.erase(scripted->second.begin());
            return result;
        }
        const auto found = playing.find(voiceHandle);
        return found != playing.end() && found->second;
    }

    void SetVolume(const std::uint32_t voiceHandle,
                   const float volume) override {
        ++setVolumeAttempts[voiceHandle];
        if (throwOnSetVolumeVoice == voiceHandle) {
            throw std::runtime_error{"volume failure"};
        }
        volumeCalls.push_back({voiceHandle, volume});
    }

    [[nodiscard]] std::uint32_t SoundHandle(
        const std::string_view relativePath) const {
        return soundHandles.at(std::string{relativePath});
    }

    bool available = true;
    std::unordered_set<std::string> availableWaves;
    std::vector<std::string> loadedPaths;
    std::unordered_map<std::string, std::uint32_t> soundHandles;
    std::vector<PlayCall> playCalls;
    std::vector<std::uint32_t> stopCalls;
    std::vector<VolumeCall> volumeCalls;
    std::unordered_map<std::uint32_t, bool> playing;
    std::unordered_map<std::uint32_t, std::vector<bool>> scriptedPlaying;
    std::unordered_map<std::uint32_t, int> isPlayingCalls;
    std::unordered_map<std::uint32_t, int> setVolumeAttempts;
    std::uint32_t nextSoundHandle = 0;
    std::uint32_t nextVoiceHandle = 0;
    std::uint32_t throwOnPlaySoundHandle = UINT32_MAX;
    std::uint32_t throwOnIsPlayingVoice = UINT32_MAX;
    std::uint32_t throwOnSetVolumeVoice = UINT32_MAX;
};

[[nodiscard]] const VolumeCall* FindLastVolume(
    const FakeAudioBackend& backend, const std::uint32_t voiceHandle) {
    for (auto call = backend.volumeCalls.rbegin();
         call != backend.volumeCalls.rend(); ++call) {
        if (call->voiceHandle == voiceHandle) {
            return &*call;
        }
    }
    return nullptr;
}

[[nodiscard]] int CountStops(const FakeAudioBackend& backend,
                             const std::uint32_t voiceHandle) {
    return static_cast<int>(
        std::count(backend.stopCalls.begin(), backend.stopCalls.end(),
                   voiceHandle));
}

void TestIntroLoopAndRestart(TestContext& context) {
    FakeAudioBackend backend;
    GameAudio audio{backend};
    audio.Initialize();

    context.Expect(backend.loadedPaths.size() == 4,
                   "Initialize loads each of the four clips exactly once");
    context.Expect(backend.playCalls.size() == 1,
                   "Initialize starts the intro immediately");
    if (!backend.playCalls.empty()) {
        const PlayCall& intro = backend.playCalls.front();
        context.Expect(intro.soundHandle == 0 && intro.voiceHandle == 0,
                       "zero-valued sound and voice handles remain valid");
        context.Expect(!intro.loop && NearlyEqual(intro.volume, 0.9f),
                       "the intro is a 0.9-volume one-shot");
    }

    audio.Update(1.0f / 60.0f);
    backend.playing[0] = false;
    audio.Update(1.0f / 60.0f);
    context.Expect(backend.playCalls.size() == 2,
                   "a completed intro starts the loop on the next update");
    if (backend.playCalls.size() >= 2) {
        const PlayCall& loop = backend.playCalls[1];
        context.Expect(loop.soundHandle == 1 && loop.loop &&
                           NearlyEqual(loop.volume, 0.4f),
                       "the loop starts once at its configured base volume");
    }
    audio.Update(1.0f / 60.0f);
    context.Expect(backend.playCalls.size() == 2,
                   "the running loop is not restarted each frame");

    audio.RestartMusicSequence();
    context.Expect(backend.playCalls.size() == 3 &&
                       backend.playCalls.back().soundHandle == 0 &&
                       !backend.playCalls.back().loop,
                   "RestartMusicSequence stops the loop and restarts the intro");
    context.Expect(CountStops(backend, 1) == 1,
                   "RestartMusicSequence stops the active loop exactly once");

    backend.scriptedPlaying[2] = {false, true};
    audio.Update(1.0f / 60.0f);
    context.Expect(backend.playCalls.size() == 3,
                   "a just-started intro false poll cannot start a second loop");
    audio.Update(1.0f / 60.0f);
    context.Expect(backend.playCalls.size() == 3,
                   "the restarted intro arms only after playback is observed");

    audio.Finalize();
    const int stoppedRestartedIntro = CountStops(backend, 2);
    audio.Finalize();
    context.Expect(stoppedRestartedIntro == 1 && CountStops(backend, 2) == 1,
                   "Finalize is idempotent for an active voice");
}

void TestMissingClipFallbacks(TestContext& context) {
    FakeAudioBackend noIntroBackend;
    noIntroBackend.availableWaves.erase("audio/bgm_start.wav");
    GameAudio noIntro{noIntroBackend};
    noIntro.Initialize();
    context.Expect(noIntroBackend.loadedPaths.size() == 3,
                   "a missing optional intro does not prevent other loads");
    context.Expect(noIntroBackend.playCalls.size() == 1 &&
                       noIntroBackend.playCalls.front().loop &&
                       NearlyEqual(noIntroBackend.playCalls.front().volume,
                                   0.4f),
                   "a missing intro falls back directly to the loop");

    FakeAudioBackend noLoopBackend;
    noLoopBackend.availableWaves.erase("audio/bgm_loop.wav");
    GameAudio noLoop{noLoopBackend};
    noLoop.Initialize();
    noLoop.Update(1.0f / 60.0f);
    noLoopBackend.playing[0] = false;
    noLoop.Update(1.0f / 60.0f);
    noLoop.Update(1.0f / 60.0f);
    context.Expect(noLoopBackend.playCalls.size() == 1,
                   "a missing loop leaves silence after the intro without retries");

    FakeAudioBackend unavailableBackend;
    unavailableBackend.available = false;
    GameAudio unavailable{unavailableBackend};
    std::vector<std::string> warnings;
    unavailable.Initialize(&warnings);
    context.Expect(unavailableBackend.loadedPaths.empty() &&
                       unavailableBackend.playCalls.empty() && !warnings.empty(),
                   "an unavailable backend degrades to warning-only silence");
}

void TestHoldMixAndEndReasons(TestContext& context) {
    FakeAudioBackend backend;
    GameAudio audio{backend};
    audio.Initialize();
    audio.BeginLineHold();
    audio.BeginLineHold();

    context.Expect(backend.playCalls.size() == 2,
                   "BeginLineHold starts one hold one-shot per drag");
    if (backend.playCalls.size() >= 2) {
        const PlayCall& hold = backend.playCalls[1];
        context.Expect(hold.soundHandle == backend.SoundHandle(
                                              "audio/line_hold.wav") &&
                           !hold.loop && NearlyEqual(hold.volume, 0.65f),
                       "line hold uses its configured one-shot mix");
    }

    audio.Update(0.025f);
    const VolumeCall* halfAttack = FindLastVolume(backend, 0);
    context.Expect(halfAttack != nullptr &&
                       NearlyEqual(halfAttack->volume, 0.63f),
                   "BGM duck reaches its linear half-attack value at 25 ms");
    audio.Update(0.025f);
    const VolumeCall* fullAttack = FindLastVolume(backend, 0);
    context.Expect(fullAttack != nullptr &&
                       NearlyEqual(fullAttack->volume, 0.36f),
                   "BGM duck reaches 0.4 gain after 50 ms");

    audio.EndLineHold(LineHoldEndReason::Released);
    audio.EndLineHold(LineHoldEndReason::Released);
    context.Expect(backend.playCalls.size() == 3 &&
                       backend.playCalls.back().soundHandle ==
                           backend.SoundHandle("audio/line_relax.wav") &&
                       NearlyEqual(backend.playCalls.back().volume, 2.0f),
                   "a true release plays relax once at its configured volume");

    audio.Update(0.025f);
    const VolumeCall* halfHoldFade = FindLastVolume(backend, 1);
    const VolumeCall* firstRelease = FindLastVolume(backend, 0);
    context.Expect(halfHoldFade != nullptr &&
                       NearlyEqual(halfHoldFade->volume, 0.325f),
                   "the hold voice fades linearly over 50 ms");
    context.Expect(firstRelease != nullptr &&
                       NearlyEqual(firstRelease->volume, 0.414f),
                   "BGM begins its 250 ms release after the drag ends");
    audio.Update(0.025f);
    context.Expect(CountStops(backend, 1) == 1,
                   "the hold voice stops when its 50 ms fade completes");
    audio.Update(0.2f);
    const VolumeCall* fullRelease = FindLastVolume(backend, 0);
    context.Expect(fullRelease != nullptr &&
                       NearlyEqual(fullRelease->volume, 0.9f),
                   "BGM returns to its base volume after 250 ms");

    const std::size_t playsBeforeCancel = backend.playCalls.size();
    audio.BeginLineHold();
    audio.EndLineHold(LineHoldEndReason::Cancelled);
    context.Expect(backend.playCalls.size() == playsBeforeCancel + 1,
                   "cancelling a hold does not play the relax sound");
    audio.Update(0.05f);
    context.Expect(CountStops(backend, 3) == 1,
                   "a cancelled hold still stops through the short fade");
}

void TestRuntimeFailuresDoNotRetry(TestContext& context) {
    FakeAudioBackend pollFailureBackend;
    GameAudio pollFailure{pollFailureBackend};
    pollFailure.Initialize();
    pollFailureBackend.throwOnIsPlayingVoice = 0;
    pollFailure.Update(1.0f / 60.0f);
    pollFailure.Update(1.0f / 60.0f);
    context.Expect(pollFailureBackend.isPlayingCalls[0] == 1 &&
                       pollFailureBackend.playCalls.size() == 1,
                   "a music polling failure disables the phase without retries");

    FakeAudioBackend volumeFailureBackend;
    GameAudio volumeFailure{volumeFailureBackend};
    volumeFailure.Initialize();
    volumeFailure.BeginLineHold();
    volumeFailureBackend.throwOnSetVolumeVoice = 0;
    volumeFailure.Update(0.01f);
    volumeFailure.Update(0.01f);
    context.Expect(volumeFailureBackend.setVolumeAttempts[0] == 1,
                   "a persistent music volume failure is not retried each frame");

    FakeAudioBackend zeroHandleBackend;
    GameAudio zeroHandle{zeroHandleBackend};
    zeroHandle.Initialize();
    zeroHandle.Finalize();
    zeroHandle.Finalize();
    context.Expect(CountStops(zeroHandleBackend, 0) == 1,
                   "Finalize stops a valid zero-valued voice exactly once");
}

} // namespace
} // namespace object_connect::tests

int main() {
    object_connect::tests::TestContext context;
    object_connect::tests::TestIntroLoopAndRestart(context);
    object_connect::tests::TestMissingClipFallbacks(context);
    object_connect::tests::TestHoldMixAndEndReasons(context);
    object_connect::tests::TestRuntimeFailuresDoNotRetry(context);

    if (context.GetFailureCount() != 0) {
        std::cerr << context.GetFailureCount()
                  << " GameAudio test(s) failed.\n";
        return 1;
    }
    std::cout << "All GameAudio tests passed.\n";
    return 0;
}
