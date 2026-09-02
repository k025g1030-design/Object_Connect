#pragma once

#include "ObjectConnect/Game/GameConfig.hpp"

#include <memory>
#include <string>

namespace object_connect {

class Game final {
public:
    Game() noexcept;
    ~Game();

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;

    [[nodiscard]] bool Initialize(std::string& error);
    [[nodiscard]] bool Initialize(const GameConfig& config, std::string& error);
    void Update(float deltaSeconds);
    void Draw();
    void Finalize() noexcept;

    [[nodiscard]] bool ShouldQuit() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace object_connect
