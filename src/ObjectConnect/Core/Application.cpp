#include "ObjectConnect/Core/Application.hpp"

#include "ObjectConnect/Core/FrameTimer.hpp"
#include "ObjectConnect/Game/Game.hpp"

#include <KamataEngine.h>
#include <Windows.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace object_connect {
namespace {

constexpr const wchar_t* kWindowTitle = L"6008_BloodLine";

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

[[nodiscard]] std::wstring Utf8MessageToWide(const std::string_view message) {
    if (message.empty()) {
        return {};
    }
    if (message.size() >
        static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        throw std::runtime_error("The diagnostic message is too long.");
    }
    const int inputLength = static_cast<int>(message.size());
    const int required = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, message.data(), inputLength, nullptr, 0);
    if (required <= 0) {
        throw std::runtime_error("The diagnostic message is not valid UTF-8.");
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (::MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, message.data(), inputLength,
            wide.data(), required) != required) {
        throw std::runtime_error("The diagnostic message could not be converted.");
    }
    return wide;
}

void ShowError(const char* const message) noexcept {
    try {
        const std::wstring wide = Utf8MessageToWide(message != nullptr
                                                        ? std::string_view{message}
                                                        : std::string_view{});
        ::MessageBoxW(nullptr, wide.c_str(), L"Object_Connect error",
                      MB_OK | MB_ICONERROR | MB_TASKMODAL);
    } catch (...) {
        ::MessageBoxA(nullptr,
                      message != nullptr ? message : "Unknown Object_Connect error.",
                      "Object_Connect error",
                      MB_OK | MB_ICONERROR | MB_TASKMODAL);
    }
}

void ShowWarnings(const std::vector<std::string>& warnings) noexcept {
    if (warnings.empty()) {
        return;
    }
    try {
        std::string message =
            "Object_Connect started with the following warnings:\n\n";
        for (const std::string& warning : warnings) {
            message += "- ";
            message += warning;
            message += '\n';
        }
        const std::wstring wide = Utf8MessageToWide(message);
        ::MessageBoxW(nullptr, wide.c_str(), L"Object_Connect warning",
                      MB_OK | MB_ICONWARNING | MB_TASKMODAL);
    } catch (...) {
        ::MessageBoxA(nullptr,
                      "Object_Connect started with warnings, but the "
                      "diagnostic text could not be prepared.",
                      "Object_Connect warning",
                      MB_OK | MB_ICONWARNING | MB_TASKMODAL);
    }
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
        ShowWarnings(game.GetStartupWarnings());

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
