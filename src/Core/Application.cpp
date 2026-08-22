#include "RetroFPS/Core/Application.hpp"

#include "RetroFPS/Core/FrameTimer.hpp"
#include "RetroFPS/Game/Game.hpp"

#include <2d/DebugText.h>
#include <KamataEngine.h>
#include <Windows.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fps {
namespace {

constexpr const wchar_t* kWindowTitle = L"Object_FPS - 2.5D FPS MVP";

[[nodiscard]] bool SetExecutableWorkingDirectory(std::wstring& error) {
    std::vector<wchar_t> executablePath(32768, L'\0');
    const DWORD length = ::GetModuleFileNameW(
        nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
    if (length == 0 || length >= executablePath.size()) {
        error = L"Unable to determine the Object_FPS executable directory.";
        return false;
    }

    const std::filesystem::path directory =
        std::filesystem::path(executablePath.data(), executablePath.data() + length).parent_path();
    std::error_code filesystemError;
    std::filesystem::current_path(directory, filesystemError);
    if (filesystemError) {
        error = L"Unable to use the Object_FPS executable directory as the working directory.";
        return false;
    }

    return true;
}

void ShowError(const char* const message) noexcept {
    ::MessageBoxA(
        nullptr,
        message,
        "Object_FPS startup error",
        MB_OK | MB_ICONERROR | MB_TASKMODAL);
}

class EngineLifetime final {
public:
    EngineLifetime() = default;

    ~EngineLifetime() {
        if (initialized_) {
            KamataEngine::Finalize();
        }
    }

    EngineLifetime(const EngineLifetime&) = delete;
    EngineLifetime& operator=(const EngineLifetime&) = delete;

    void Initialize() {
        KamataEngine::Initialize(kWindowTitle);
        initialized_ = true;

        // KamataEngine::Initialize sets up Sprite, but DebugText is an optional
        // service and must be initialized separately before its first Print.
        KamataEngine::DebugText::GetInstance()->Initialize();
        KamataEngine::WinApp::GetInstance()->SetSizeChangeMode(
            KamataEngine::WinApp::SizeChangeMode::kNone);
    }

private:
    bool initialized_ = false;
};

class GameSession final {
public:
    explicit GameSession(Game& game) noexcept
        : game_(game) {}

    ~GameSession() { game_.Finalize(); }

    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

private:
    Game& game_;
};

} // namespace

int Application::Run(Game& game) noexcept {
    try {
        std::wstring workingDirectoryError;
        if (!SetExecutableWorkingDirectory(workingDirectoryError)) {
            ::MessageBoxW(
                nullptr,
                workingDirectoryError.c_str(),
                L"Object_FPS startup error",
                MB_OK | MB_ICONERROR | MB_TASKMODAL);
            return EXIT_FAILURE;
        }

        EngineLifetime engine;
        engine.Initialize();
        [[maybe_unused]] GameSession gameSession{game};

        std::string runtimeError;
        if (!game.Initialize(runtimeError)) {
            ShowError(runtimeError.c_str());
            return EXIT_FAILURE;
        }

        KamataEngine::DirectXCommon* const directX =
            KamataEngine::DirectXCommon::GetInstance();
        FrameTimer frameTimer;
        frameTimer.Reset();

        while (!KamataEngine::Update()) {
            game.Update(frameTimer.Tick());
            if (game.ShouldQuit()) {
                break;
            }

            directX->PreDraw();
            game.Draw();
            directX->PostDraw();
        }

        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        ShowError(exception.what());
    } catch (...) {
        ShowError("Object_FPS stopped because of an unknown error.");
    }

    return EXIT_FAILURE;
}

} // namespace fps
