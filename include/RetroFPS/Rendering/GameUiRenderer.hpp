#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace fps {

enum class GameUiScreen {
    MainMenu,
    Controls,
    PauseMenu,
    Results,
};

struct UiPoint final {
    float x = 0.0f;
    float y = 0.0f;
};

struct ResultRoomEntry final {
    std::string levelName;
    std::size_t kills = 0;
};

struct ResultsUiView final {
    bool campaignCompleted = false;
    std::span<const ResultRoomEntry> rooms{};
};

// ASCII menu presentation and mouse hit testing. Item indices are ordered as drawn:
// MainMenu = Start, Controls, Exit; Controls = Back;
// PauseMenu = Resume, Main Menu, Exit Game; Results = Main Menu.
class GameUiRenderer final {
public:
    GameUiRenderer() noexcept;
    ~GameUiRenderer();

    GameUiRenderer(const GameUiRenderer&) = delete;
    GameUiRenderer& operator=(const GameUiRenderer&) = delete;
    GameUiRenderer(GameUiRenderer&&) = delete;
    GameUiRenderer& operator=(GameUiRenderer&&) = delete;

    [[nodiscard]] bool Initialize(std::string& error);
    void Draw(
        GameUiScreen screen,
        std::size_t selectedIndex,
        const ResultsUiView* results = nullptr) const;
    void Finalize() noexcept;

    [[nodiscard]] std::optional<std::size_t> HitTest(GameUiScreen screen,
                                                     UiPoint point) const noexcept;
    [[nodiscard]] static std::size_t GetItemCount(GameUiScreen screen) noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fps
