#include "ObjectConnect/Core/Application.hpp"

#include "ObjectConnect/Core/FrameTimer.hpp"
#include "ObjectConnect/Game/Game.hpp"

#include <2d/DebugText.h>
#include <KamataEngine.h>
#include <Windows.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace object_connect {
namespace {

constexpr const wchar_t* kWindowTitle = L"Object_Connect";

#if defined(_DEBUG)
constexpr bool kEnableDirectXDebugLayer = true;
#else
constexpr bool kEnableDirectXDebugLayer = false;
#endif

[[nodiscard]] bool SetExecutableWorkingDirectory(std::wstring& error) {
    std::vector<wchar_t> executablePath(32768, L'\0');
    const DWORD length = ::GetModuleFileNameW(
        nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
    if (length == 0 || length >= executablePath.size()) {
        error = L"Unable to determine the Object_Connect executable directory.";
        return false;
    }

    const std::filesystem::path directory =
        std::filesystem::path(executablePath.data(), executablePath.data() + length)
            .parent_path();
    std::error_code filesystemError;
    std::filesystem::current_path(directory, filesystemError);
    if (filesystemError) {
        error = L"Unable to use the Object_Connect executable directory as the working directory.";
        return false;
    }
    return true;
}

void ShowError(const char* const message) noexcept {
    ::MessageBoxA(nullptr, message, "Object_Connect error",
                  MB_OK | MB_ICONERROR | MB_TASKMODAL);
}

class EngineLifetime final {
public:
    ~EngineLifetime() {
        if (initialized_) {
            KamataEngine::Finalize();
        }
    }

    void Initialize() {
        KamataEngine::Initialize(kWindowTitle, kEnableDirectXDebugLayer);
        initialized_ = true;
        KamataEngine::DebugText::GetInstance()->Initialize();
        KamataEngine::WinApp::GetInstance()->SetSizeChangeMode(
            KamataEngine::WinApp::SizeChangeMode::kNone);
    }

private:
    bool initialized_ = false;
};

class GameLifetime final {
public:
    explicit GameLifetime(Game& game) noexcept : game_(game) {}
    ~GameLifetime() { game_.Finalize(); }

private:
    Game& game_;
};

} // namespace

int Application::Run(Game& game) noexcept {
    try {
        std::wstring pathError;
        if (!SetExecutableWorkingDirectory(pathError)) {
            ::MessageBoxW(nullptr, pathError.c_str(), L"Object_Connect error",
                          MB_OK | MB_ICONERROR | MB_TASKMODAL);
            return EXIT_FAILURE;
        }

        EngineLifetime engine;
        engine.Initialize();
        [[maybe_unused]] GameLifetime gameLifetime{game};

        std::string error;
        if (!game.Initialize(error)) {
            ShowError(error.c_str());
            return EXIT_FAILURE;
        }

        KamataEngine::DirectXCommon* const directX =
            KamataEngine::DirectXCommon::GetInstance();
        FrameTimer timer;
        timer.Reset();
        while (!KamataEngine::Update()) {
            game.Update(timer.Tick());
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
        ShowError("Object_Connect stopped because of an unknown error.");
    }
    return EXIT_FAILURE;
}

} // namespace object_connect
