#pragma once

namespace object_connect {

class Game;

class Application final {
public:
    [[nodiscard]] int Run(Game& game) noexcept;
};

} // namespace object_connect
