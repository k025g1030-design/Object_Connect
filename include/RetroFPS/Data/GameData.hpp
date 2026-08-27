#pragma once

#include "RetroFPS/World/GridMap.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fps {

using EnemyDefinitionId = std::string;
using WeaponDefinitionId = std::string;
using LevelDefinitionId = std::string;

struct EnemyAnimationPixelPoint final {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

struct EnemyAnimationClipDefinition final {
    std::uint32_t originXpx = 0;
    std::uint32_t originYpx = 0;
    std::uint32_t frameCount = 0;
    float secondsPerFrame = 0.0f;
    std::optional<std::uint32_t> eventFrameIndex;
    std::optional<EnemyAnimationPixelPoint> muzzlePixel;
};

struct EnemyAnimationSetDefinition final {
    EnemyAnimationClipDefinition idle;
    EnemyAnimationClipDefinition moving;
    EnemyAnimationClipDefinition attacking;
    EnemyAnimationClipDefinition dead;
};

struct EnemyDefinition final {
    EnemyDefinitionId id;
    EnemyKind kind = EnemyKind::Melee;
    float damage = 0.0f;
    float attackIntervalSeconds = 0.0f;
    float maxHealth = 0.0f;
    float defense = 0.0f;
    float hitboxRadius = 0.0f;
    float hitboxHeight = 0.0f;
    float renderWidth = 0.0f;
    float renderHeight = 0.0f;
    std::string texturePath;
    std::uint32_t frameWidthPixels = 0;
    std::uint32_t frameHeightPixels = 0;
    EnemyAnimationSetDefinition animations;
};

struct WeaponDefinition final {
    WeaponDefinitionId id;
    float damage = 0.0f;
    std::uint32_t magazineCapacity = 0;
    std::uint32_t reserveAmmo = 0;
    float recoilDegrees = 0.0f;
    bool automatic = false;
    std::string texturePath;
    float fireIntervalSeconds = 0.0f;
    float reloadSeconds = 0.0f;
};

struct LevelDefinition final {
    LevelDefinitionId id;
    std::string name;
    std::filesystem::path mapPath;
    std::optional<LevelDefinitionId> nextLevelId;
    std::uint32_t rangedEnemyCount = 0;
    std::uint32_t meleeEnemyCount = 0;
    std::uint32_t activeEnemyLimit = 0;
    std::uint32_t clearKillCount = 0;
};

class EnemyCatalog final {
public:
    [[nodiscard]] std::span<const EnemyDefinition> GetDefinitions() const noexcept {
        return definitions_;
    }
    [[nodiscard]] const EnemyDefinition* FindById(std::string_view id) const noexcept;
    [[nodiscard]] const EnemyDefinition* FindByKind(EnemyKind kind) const noexcept;

private:
    friend class GameDataLoader;
    std::vector<EnemyDefinition> definitions_;
};

class WeaponCatalog final {
public:
    [[nodiscard]] std::span<const WeaponDefinition> GetDefinitions() const noexcept {
        return definitions_;
    }
    [[nodiscard]] const WeaponDefinition* FindById(std::string_view id) const noexcept;
    [[nodiscard]] const WeaponDefinition* GetDefaultWeapon() const noexcept;

private:
    friend class GameDataLoader;
    std::vector<WeaponDefinition> definitions_;
};

class LevelCatalog final {
public:
    [[nodiscard]] std::span<const LevelDefinition> GetDefinitions() const noexcept {
        return definitions_;
    }
    [[nodiscard]] const LevelDefinition* FindById(std::string_view id) const noexcept;
    [[nodiscard]] const LevelDefinition* GetStartLevel() const noexcept;

private:
    friend class GameDataLoader;
    std::vector<LevelDefinition> definitions_;
};

struct GameDataCatalog final {
    EnemyCatalog enemies;
    WeaponCatalog weapons;
    LevelCatalog levels;
};

struct GameDataPaths final {
    std::filesystem::path enemiesCsvPath{"Resources/data/enemies.csv"};
    std::filesystem::path enemyAnimationClipsCsvPath{
        "Resources/data/enemy_animation_clips.csv"};
    std::filesystem::path weaponsCsvPath{"Resources/data/weapons.csv"};
    std::filesystem::path levelsCsvPath{"Resources/data/levels.csv"};
    std::filesystem::path resourceRoot{"Resources"};
};

struct GameDataLoadResult final {
    std::optional<GameDataCatalog> catalog;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept { return catalog.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return Succeeded(); }
};

class GameDataLoader final {
public:
    [[nodiscard]] static GameDataLoadResult Load(const GameDataPaths& paths);
    [[nodiscard]] static GameDataLoadResult Parse(
        std::string_view enemiesCsv,
        std::string_view enemyAnimationClipsCsv,
        std::string_view weaponsCsv,
        std::string_view levelsCsv,
        const std::filesystem::path& resourceRoot);
};

} // namespace fps
