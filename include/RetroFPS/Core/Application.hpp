#pragma once

namespace fps {

class Game;

// プラットフォーム／エンジンのライフタイムを管理し、1つのGameを実行する。
class Application final {
public:
    [[nodiscard]] int Run(Game& game) noexcept;
};

} // namespace fps
