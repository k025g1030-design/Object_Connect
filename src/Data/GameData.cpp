#include "RetroFPS/Data/GameData.hpp"

#include "RetroFPS/Data/Csv.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fps {
namespace {

constexpr std::string_view kEnemyCatalogName{"enemies.csv"};
constexpr std::string_view kEnemyAnimationCatalogName{"enemy_animation_clips.csv"};
constexpr std::string_view kWeaponCatalogName{"weapons.csv"};
constexpr std::string_view kLevelCatalogName{"levels.csv"};

const std::vector<std::string> kEnemyHeader{
    "enemy_id",
    "kind",
    "damage",
    "attack_interval_seconds",
    "hp",
    "defense",
    "hitbox_radius",
    "hitbox_height",
    "render_width",
    "render_height",
    "texture_name",
    "frame_width_px",
    "frame_height_px",
};
const std::vector<std::string> kEnemyAnimationHeader{
    "enemy_id",
    "state",
    "origin_x_px",
    "origin_y_px",
    "frame_count",
    "seconds_per_frame",
    "event_frame_index",
    "muzzle_x_px",
    "muzzle_y_px",
};
const std::vector<std::string> kWeaponHeader{
    "weapon_id",
    "damage",
    "magazine_size",
    "reserve_ammo",
    "recoil",
    "automatic",
    "fire_interval_seconds",
    "reload_seconds",
    "texture_name",
};
const std::vector<std::string> kLevelHeader{
    "level_id",
    "level_name",
    "map_path",
    "next_level_id",
    "ranged_enemy_count",
    "melee_enemy_count",
    "active_enemy_limit",
    "clear_kill_count",
};

class GameDataError final : public std::runtime_error {
public:
    explicit GameDataError(const std::string& message)
        : std::runtime_error(message) {}
};

struct ParsedCatalogData final {
    std::vector<EnemyDefinition> enemies;
    std::vector<WeaponDefinition> weapons;
    std::vector<LevelDefinition> levels;
};

[[nodiscard]] std::string JoinHeader(const std::vector<std::string>& header) {
    std::string result;
    for (std::size_t index = 0; index < header.size(); ++index) {
        if (index != 0) {
            result.push_back(',');
        }
        result += header[index];
    }
    return result;
}

void ValidateHeader(
    const data::CsvDocument& document,
    const std::vector<std::string>& expected,
    const std::string_view catalogName) {
    if (document.header == expected) {
        return;
    }
    throw GameDataError(
        std::string{catalogName} + " header must be exactly: " + JoinHeader(expected));
}

[[noreturn]] void ThrowFieldError(
    const std::string_view catalogName,
    const data::CsvRecord& record,
    const std::string_view fieldName,
    const std::string& detail) {
    const std::vector<std::string>* header = nullptr;
    if (catalogName == kEnemyCatalogName) {
        header = &kEnemyHeader;
    } else if (catalogName == kEnemyAnimationCatalogName) {
        header = &kEnemyAnimationHeader;
    } else if (catalogName == kWeaponCatalogName) {
        header = &kWeaponHeader;
    } else if (catalogName == kLevelCatalogName) {
        header = &kLevelHeader;
    }
    std::size_t column = 0;
    if (header != nullptr) {
        const auto found = std::ranges::find(*header, fieldName);
        if (found != header->end()) {
            column = static_cast<std::size_t>(std::distance(header->begin(), found)) + 1;
        }
    }
    throw GameDataError(
        std::string{catalogName} + " line " + std::to_string(record.lineNumber) +
        ", column " + std::to_string(column) + ", field '" +
        std::string{fieldName} + "': " + detail);
}

[[nodiscard]] std::uint32_t ParseUnsigned(
    const std::string& text,
    const std::string_view catalogName,
    const data::CsvRecord& record,
    const std::string_view fieldName) {
    std::uint32_t value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value, 10);
    if (text.empty() || result.ec != std::errc{} || result.ptr != end) {
        ThrowFieldError(catalogName, record, fieldName, "expected an unsigned 32-bit integer");
    }
    return value;
}

[[nodiscard]] std::string ParseDefinitionId(
    const std::string& text,
    const std::string_view catalogName,
    const data::CsvRecord& record,
    const std::string_view fieldName) {
    if (text.empty() || text.front() < 'a' || text.front() > 'z' ||
        text.back() == '_') {
        ThrowFieldError(
            catalogName,
            record,
            fieldName,
            "ID must use lower_snake_case and begin with a lowercase letter");
    }
    bool previousUnderscore = false;
    for (const char character : text) {
        const bool lowercaseLetter = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        const bool underscore = character == '_';
        if ((!lowercaseLetter && !digit && !underscore) ||
            (underscore && previousUnderscore)) {
            ThrowFieldError(
                catalogName,
                record,
                fieldName,
                "ID must use lower_snake_case and contain no repeated underscores");
        }
        previousUnderscore = underscore;
    }
    return text;
}

[[nodiscard]] float ParseFloat(
    const std::string& text,
    const std::string_view catalogName,
    const data::CsvRecord& record,
    const std::string_view fieldName) {
    float value = 0.0f;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result result =
        std::from_chars(begin, end, value, std::chars_format::general);
    if (text.empty() || result.ec != std::errc{} || result.ptr != end || !std::isfinite(value)) {
        ThrowFieldError(catalogName, record, fieldName, "expected a finite decimal number");
    }
    return value;
}

[[nodiscard]] bool ParseBoolean(
    const std::string& text,
    const data::CsvRecord& record,
    const std::string_view fieldName) {
    if (text == "true") {
        return true;
    }
    if (text == "false") {
        return false;
    }
    ThrowFieldError(kWeaponCatalogName, record, fieldName, "expected exactly 'true' or 'false'");
}

[[nodiscard]] bool HasVisibleText(const std::string& text) noexcept {
    return std::any_of(text.begin(), text.end(), [](const unsigned char character) {
        return character != ' ' && character != '\t' && character != '\r' && character != '\n';
    });
}

[[nodiscard]] std::string ValidateResourcePath(
    const std::string& text,
    const std::filesystem::path& resourceRoot,
    const std::string_view catalogName,
    const data::CsvRecord& record,
    const std::string_view fieldName) {
    if (text.empty() || text.find('\0') != std::string::npos ||
        text.find('\r') != std::string::npos || text.find('\n') != std::string::npos) {
        ThrowFieldError(catalogName, record, fieldName, "path must be non-empty and single-line");
    }
    if (text.find(':') != std::string::npos) {
        ThrowFieldError(catalogName, record, fieldName, "path must not contain a drive or stream separator");
    }

    const std::filesystem::path relative{text};
    if (relative.has_root_path() || relative.has_root_name() || relative.filename().empty()) {
        ThrowFieldError(catalogName, record, fieldName, "path must be relative to the resource root");
    }
    for (const std::filesystem::path& component : relative) {
        if (component.empty() || component == "." || component == "..") {
            ThrowFieldError(
                catalogName,
                record,
                fieldName,
                "path must not contain empty, current-directory, or parent-directory components");
        }
    }

    const std::filesystem::path asset = resourceRoot / relative;
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(asset, fileError)) {
        std::string detail = "referenced resource does not exist: " + asset.generic_string();
        if (fileError) {
            detail += " (" + fileError.message() + ")";
        }
        ThrowFieldError(catalogName, record, fieldName, detail);
    }
    return text;
}

void ValidatePositive(
    const float value,
    const std::string_view catalogName,
    const data::CsvRecord& record,
    const std::string_view fieldName) {
    if (value <= 0.0f) {
        ThrowFieldError(catalogName, record, fieldName, "value must be greater than zero");
    }
}

void ValidateNonNegative(
    const float value,
    const std::string_view catalogName,
    const data::CsvRecord& record,
    const std::string_view fieldName) {
    if (value < 0.0f) {
        ThrowFieldError(catalogName, record, fieldName, "value must be non-negative");
    }
}

[[nodiscard]] std::vector<EnemyDefinition> ParseEnemies(
    const data::CsvDocument& document,
    const std::filesystem::path& resourceRoot) {
    ValidateHeader(document, kEnemyHeader, kEnemyCatalogName);
    if (document.records.empty()) {
        throw GameDataError("enemy CSV must contain at least one data record");
    }

    std::vector<EnemyDefinition> definitions;
    definitions.reserve(document.records.size());
    std::unordered_set<EnemyDefinitionId> ids;
    bool hasMelee = false;
    bool hasRanged = false;

    for (const data::CsvRecord& record : document.records) {
        EnemyDefinition definition;
        definition.id = ParseDefinitionId(
            record.fields[0], kEnemyCatalogName, record, kEnemyHeader[0]);
        if (!ids.insert(definition.id).second) {
            ThrowFieldError(kEnemyCatalogName, record, kEnemyHeader[0], "duplicate enemy ID");
        }

        if (record.fields[1] == "melee") {
            if (hasMelee) {
                ThrowFieldError(
                    kEnemyCatalogName, record, kEnemyHeader[1], "only one melee definition is allowed");
            }
            hasMelee = true;
            definition.kind = EnemyKind::Melee;
        } else if (record.fields[1] == "ranged") {
            if (hasRanged) {
                ThrowFieldError(
                    kEnemyCatalogName, record, kEnemyHeader[1], "only one ranged definition is allowed");
            }
            hasRanged = true;
            definition.kind = EnemyKind::Ranged;
        } else {
            ThrowFieldError(
                kEnemyCatalogName, record, kEnemyHeader[1], "expected exactly 'melee' or 'ranged'");
        }

        definition.damage = ParseFloat(
            record.fields[2], kEnemyCatalogName, record, kEnemyHeader[2]);
        definition.attackIntervalSeconds = ParseFloat(
            record.fields[3], kEnemyCatalogName, record, kEnemyHeader[3]);
        definition.maxHealth = ParseFloat(
            record.fields[4], kEnemyCatalogName, record, kEnemyHeader[4]);
        definition.defense = ParseFloat(
            record.fields[5], kEnemyCatalogName, record, kEnemyHeader[5]);
        definition.hitboxRadius = ParseFloat(
            record.fields[6], kEnemyCatalogName, record, kEnemyHeader[6]);
        definition.hitboxHeight = ParseFloat(
            record.fields[7], kEnemyCatalogName, record, kEnemyHeader[7]);
        definition.renderWidth = ParseFloat(
            record.fields[8], kEnemyCatalogName, record, kEnemyHeader[8]);
        definition.renderHeight = ParseFloat(
            record.fields[9], kEnemyCatalogName, record, kEnemyHeader[9]);
        ValidatePositive(definition.damage, kEnemyCatalogName, record, kEnemyHeader[2]);
        ValidatePositive(
            definition.attackIntervalSeconds, kEnemyCatalogName, record, kEnemyHeader[3]);
        ValidatePositive(definition.maxHealth, kEnemyCatalogName, record, kEnemyHeader[4]);
        ValidateNonNegative(definition.defense, kEnemyCatalogName, record, kEnemyHeader[5]);
        ValidatePositive(definition.hitboxRadius, kEnemyCatalogName, record, kEnemyHeader[6]);
        ValidatePositive(definition.hitboxHeight, kEnemyCatalogName, record, kEnemyHeader[7]);
        ValidatePositive(definition.renderWidth, kEnemyCatalogName, record, kEnemyHeader[8]);
        ValidatePositive(definition.renderHeight, kEnemyCatalogName, record, kEnemyHeader[9]);
        definition.texturePath = ValidateResourcePath(
            record.fields[10], resourceRoot, kEnemyCatalogName, record, kEnemyHeader[10]);
        definition.frameWidthPixels = ParseUnsigned(
            record.fields[11], kEnemyCatalogName, record, kEnemyHeader[11]);
        definition.frameHeightPixels = ParseUnsigned(
            record.fields[12], kEnemyCatalogName, record, kEnemyHeader[12]);
        if (definition.frameWidthPixels == 0) {
            ThrowFieldError(
                kEnemyCatalogName, record, kEnemyHeader[11], "value must be greater than zero");
        }
        if (definition.frameHeightPixels == 0) {
            ThrowFieldError(
                kEnemyCatalogName, record, kEnemyHeader[12], "value must be greater than zero");
        }
        definitions.push_back(std::move(definition));
    }

    if (!hasMelee || !hasRanged) {
        throw GameDataError("enemy CSV must contain exactly one melee and one ranged definition");
    }
    return definitions;
}

enum class EnemyAnimationState : std::uint8_t {
    Idle = 0,
    Moving = 1,
    Attacking = 2,
    Dead = 3,
};

[[nodiscard]] EnemyAnimationState ParseEnemyAnimationState(
    const std::string& text,
    const data::CsvRecord& record) {
    if (text == "idle") {
        return EnemyAnimationState::Idle;
    }
    if (text == "move") {
        return EnemyAnimationState::Moving;
    }
    if (text == "attack") {
        return EnemyAnimationState::Attacking;
    }
    if (text == "dead") {
        return EnemyAnimationState::Dead;
    }
    ThrowFieldError(
        kEnemyAnimationCatalogName,
        record,
        kEnemyAnimationHeader[1],
        "expected exactly 'idle', 'move', 'attack', or 'dead'");
}

[[nodiscard]] std::string_view EnemyAnimationStateName(const EnemyAnimationState state) noexcept {
    switch (state) {
    case EnemyAnimationState::Idle:
        return "idle";
    case EnemyAnimationState::Moving:
        return "move";
    case EnemyAnimationState::Attacking:
        return "attack";
    case EnemyAnimationState::Dead:
        return "dead";
    }
    return "unknown";
}

[[nodiscard]] EnemyAnimationClipDefinition& SelectEnemyAnimationClip(
    EnemyAnimationSetDefinition& animations,
    const EnemyAnimationState state) noexcept {
    switch (state) {
    case EnemyAnimationState::Idle:
        return animations.idle;
    case EnemyAnimationState::Moving:
        return animations.moving;
    case EnemyAnimationState::Attacking:
        return animations.attacking;
    case EnemyAnimationState::Dead:
        return animations.dead;
    }
    return animations.idle;
}

void ValidateAnimationRectangle(
    const EnemyDefinition& enemy,
    const EnemyAnimationClipDefinition& clip,
    const data::CsvRecord& record) {
    constexpr std::uint64_t kMaxPixelCoordinate =
        std::numeric_limits<std::uint32_t>::max();
    constexpr std::uint64_t kMaxArithmeticValue =
        std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t originX = clip.originXpx;
    const std::uint64_t originY = clip.originYpx;
    const std::uint64_t frameWidth = enemy.frameWidthPixels;
    const std::uint64_t frameHeight = enemy.frameHeightPixels;
    const std::uint64_t frameCount = clip.frameCount;

    if (frameCount != 0 && frameWidth > kMaxArithmeticValue / frameCount) {
        ThrowFieldError(
            kEnemyAnimationCatalogName,
            record,
            kEnemyAnimationHeader[4],
            "atlas rectangle arithmetic overflows an unsigned 64-bit integer");
    }
    const std::uint64_t stripWidth = frameWidth * frameCount;
    if (originX > kMaxArithmeticValue - stripWidth ||
        originY > kMaxArithmeticValue - frameHeight) {
        ThrowFieldError(
            kEnemyAnimationCatalogName,
            record,
            kEnemyAnimationHeader[2],
            "atlas rectangle arithmetic overflows an unsigned 64-bit integer");
    }
    if (originX + stripWidth > kMaxPixelCoordinate ||
        originY + frameHeight > kMaxPixelCoordinate) {
        ThrowFieldError(
            kEnemyAnimationCatalogName,
            record,
            kEnemyAnimationHeader[2],
            "atlas rectangle arithmetic overflows unsigned 32-bit pixel coordinates");
    }
}

void ParseEnemyAnimations(
    const data::CsvDocument& document,
    std::vector<EnemyDefinition>& enemies) {
    ValidateHeader(document, kEnemyAnimationHeader, kEnemyAnimationCatalogName);

    std::unordered_map<EnemyDefinitionId, std::size_t> enemyIndices;
    enemyIndices.reserve(enemies.size());
    for (std::size_t index = 0; index < enemies.size(); ++index) {
        enemyIndices.emplace(enemies[index].id, index);
    }

    std::vector<std::uint8_t> seenStates(enemies.size(), 0);
    for (const data::CsvRecord& record : document.records) {
        const EnemyDefinitionId enemyId = ParseDefinitionId(
            record.fields[0],
            kEnemyAnimationCatalogName,
            record,
            kEnemyAnimationHeader[0]);
        const auto enemyFound = enemyIndices.find(enemyId);
        if (enemyFound == enemyIndices.end()) {
            ThrowFieldError(
                kEnemyAnimationCatalogName,
                record,
                kEnemyAnimationHeader[0],
                "referenced enemy ID does not exist");
        }

        EnemyDefinition& enemy = enemies[enemyFound->second];
        const EnemyAnimationState state = ParseEnemyAnimationState(record.fields[1], record);
        const std::uint8_t stateBit =
            static_cast<std::uint8_t>(1U << static_cast<std::uint8_t>(state));
        if ((seenStates[enemyFound->second] & stateBit) != 0) {
            ThrowFieldError(
                kEnemyAnimationCatalogName,
                record,
                kEnemyAnimationHeader[1],
                "duplicate animation state for enemy");
        }

        EnemyAnimationClipDefinition clip;
        clip.originXpx = ParseUnsigned(
            record.fields[2],
            kEnemyAnimationCatalogName,
            record,
            kEnemyAnimationHeader[2]);
        clip.originYpx = ParseUnsigned(
            record.fields[3],
            kEnemyAnimationCatalogName,
            record,
            kEnemyAnimationHeader[3]);
        clip.frameCount = ParseUnsigned(
            record.fields[4],
            kEnemyAnimationCatalogName,
            record,
            kEnemyAnimationHeader[4]);
        if (clip.frameCount == 0) {
            ThrowFieldError(
                kEnemyAnimationCatalogName,
                record,
                kEnemyAnimationHeader[4],
                "value must be greater than zero");
        }
        clip.secondsPerFrame = ParseFloat(
            record.fields[5],
            kEnemyAnimationCatalogName,
            record,
            kEnemyAnimationHeader[5]);
        ValidatePositive(
            clip.secondsPerFrame,
            kEnemyAnimationCatalogName,
            record,
            kEnemyAnimationHeader[5]);

        if (state == EnemyAnimationState::Attacking) {
            if (record.fields[6].empty()) {
                ThrowFieldError(
                    kEnemyAnimationCatalogName,
                    record,
                    kEnemyAnimationHeader[6],
                    "attack animation requires an event frame index");
            }
            clip.eventFrameIndex = ParseUnsigned(
                record.fields[6],
                kEnemyAnimationCatalogName,
                record,
                kEnemyAnimationHeader[6]);
            if (*clip.eventFrameIndex >= clip.frameCount) {
                ThrowFieldError(
                    kEnemyAnimationCatalogName,
                    record,
                    kEnemyAnimationHeader[6],
                    "event frame index must be less than frame count");
            }
        } else if (!record.fields[6].empty()) {
            ThrowFieldError(
                kEnemyAnimationCatalogName,
                record,
                kEnemyAnimationHeader[6],
                "event frame index must be empty for non-attack animations");
        }

        const bool hasMuzzleX = !record.fields[7].empty();
        const bool hasMuzzleY = !record.fields[8].empty();
        const bool needsMuzzle =
            state == EnemyAnimationState::Attacking && enemy.kind == EnemyKind::Ranged;
        if (needsMuzzle) {
            if (!hasMuzzleX || !hasMuzzleY) {
                ThrowFieldError(
                    kEnemyAnimationCatalogName,
                    record,
                    !hasMuzzleX ? kEnemyAnimationHeader[7] : kEnemyAnimationHeader[8],
                    "ranged attack animation requires both muzzle pixel coordinates");
            }
            const EnemyAnimationPixelPoint muzzle{
                ParseUnsigned(
                    record.fields[7],
                    kEnemyAnimationCatalogName,
                    record,
                    kEnemyAnimationHeader[7]),
                ParseUnsigned(
                    record.fields[8],
                    kEnemyAnimationCatalogName,
                    record,
                    kEnemyAnimationHeader[8]),
            };
            if (muzzle.x >= enemy.frameWidthPixels) {
                ThrowFieldError(
                    kEnemyAnimationCatalogName,
                    record,
                    kEnemyAnimationHeader[7],
                    "muzzle x coordinate must be inside one animation frame");
            }
            if (muzzle.y >= enemy.frameHeightPixels) {
                ThrowFieldError(
                    kEnemyAnimationCatalogName,
                    record,
                    kEnemyAnimationHeader[8],
                    "muzzle y coordinate must be inside one animation frame");
            }
            clip.muzzlePixel = muzzle;
        } else if (hasMuzzleX || hasMuzzleY) {
            ThrowFieldError(
                kEnemyAnimationCatalogName,
                record,
                hasMuzzleX ? kEnemyAnimationHeader[7] : kEnemyAnimationHeader[8],
                "muzzle coordinates must be empty except for ranged attack animations");
        }

        ValidateAnimationRectangle(enemy, clip, record);
        SelectEnemyAnimationClip(enemy.animations, state) = clip;
        seenStates[enemyFound->second] |= stateBit;
    }

    constexpr std::uint8_t kAllStates = 0x0F;
    for (std::size_t enemyIndex = 0; enemyIndex < enemies.size(); ++enemyIndex) {
        if (seenStates[enemyIndex] == kAllStates) {
            continue;
        }
        for (std::uint8_t stateIndex = 0; stateIndex < 4; ++stateIndex) {
            const std::uint8_t stateBit = static_cast<std::uint8_t>(1U << stateIndex);
            if ((seenStates[enemyIndex] & stateBit) == 0) {
                throw GameDataError(
                    "enemy_animation_clips.csv is missing state '" +
                    std::string{EnemyAnimationStateName(
                        static_cast<EnemyAnimationState>(stateIndex))} +
                    "' for enemy '" + enemies[enemyIndex].id + "'");
            }
        }
    }
}

[[nodiscard]] std::vector<WeaponDefinition> ParseWeapons(
    const data::CsvDocument& document,
    const std::filesystem::path& resourceRoot) {
    ValidateHeader(document, kWeaponHeader, kWeaponCatalogName);
    if (document.records.empty()) {
        throw GameDataError("weapon CSV must contain at least one data record");
    }

    std::vector<WeaponDefinition> definitions;
    definitions.reserve(document.records.size());
    std::unordered_set<WeaponDefinitionId> ids;
    for (const data::CsvRecord& record : document.records) {
        WeaponDefinition definition;
        definition.id = ParseDefinitionId(
            record.fields[0], kWeaponCatalogName, record, kWeaponHeader[0]);
        if (!ids.insert(definition.id).second) {
            ThrowFieldError(kWeaponCatalogName, record, kWeaponHeader[0], "duplicate weapon ID");
        }

        definition.damage = ParseFloat(
            record.fields[1], kWeaponCatalogName, record, kWeaponHeader[1]);
        definition.magazineCapacity = ParseUnsigned(
            record.fields[2], kWeaponCatalogName, record, kWeaponHeader[2]);
        definition.reserveAmmo = ParseUnsigned(
            record.fields[3], kWeaponCatalogName, record, kWeaponHeader[3]);
        definition.recoilDegrees = ParseFloat(
            record.fields[4], kWeaponCatalogName, record, kWeaponHeader[4]);
        definition.automatic = ParseBoolean(record.fields[5], record, kWeaponHeader[5]);
        definition.fireIntervalSeconds = ParseFloat(
            record.fields[6], kWeaponCatalogName, record, kWeaponHeader[6]);
        definition.reloadSeconds = ParseFloat(
            record.fields[7], kWeaponCatalogName, record, kWeaponHeader[7]);
        definition.texturePath = ValidateResourcePath(
            record.fields[8], resourceRoot, kWeaponCatalogName, record, kWeaponHeader[8]);

        ValidatePositive(definition.damage, kWeaponCatalogName, record, kWeaponHeader[1]);
        if (definition.magazineCapacity == 0) {
            ThrowFieldError(
                kWeaponCatalogName, record, kWeaponHeader[2], "value must be greater than zero");
        }
        ValidateNonNegative(
            definition.recoilDegrees, kWeaponCatalogName, record, kWeaponHeader[4]);
        ValidatePositive(
            definition.fireIntervalSeconds, kWeaponCatalogName, record, kWeaponHeader[6]);
        ValidatePositive(
            definition.reloadSeconds, kWeaponCatalogName, record, kWeaponHeader[7]);
        definitions.push_back(std::move(definition));
    }
    return definitions;
}

[[nodiscard]] std::vector<LevelDefinition> ParseLevels(
    const data::CsvDocument& document,
    const std::filesystem::path& resourceRoot) {
    ValidateHeader(document, kLevelHeader, kLevelCatalogName);
    if (document.records.empty()) {
        throw GameDataError("level CSV must contain at least one data record");
    }

    std::vector<LevelDefinition> definitions;
    definitions.reserve(document.records.size());
    std::unordered_set<LevelDefinitionId> ids;
    for (const data::CsvRecord& record : document.records) {
        LevelDefinition definition;
        definition.id = ParseDefinitionId(
            record.fields[0], kLevelCatalogName, record, kLevelHeader[0]);
        if (!ids.insert(definition.id).second) {
            ThrowFieldError(kLevelCatalogName, record, kLevelHeader[0], "duplicate level ID");
        }

        definition.name = record.fields[1];
        if (!HasVisibleText(definition.name)) {
            ThrowFieldError(kLevelCatalogName, record, kLevelHeader[1], "name must not be empty");
        }
        definition.mapPath = ValidateResourcePath(
            record.fields[2], resourceRoot, kLevelCatalogName, record, kLevelHeader[2]);
        if (!record.fields[3].empty()) {
            definition.nextLevelId = ParseDefinitionId(
                record.fields[3], kLevelCatalogName, record, kLevelHeader[3]);
        }
        definition.rangedEnemyCount = ParseUnsigned(
            record.fields[4], kLevelCatalogName, record, kLevelHeader[4]);
        definition.meleeEnemyCount = ParseUnsigned(
            record.fields[5], kLevelCatalogName, record, kLevelHeader[5]);
        definition.activeEnemyLimit = ParseUnsigned(
            record.fields[6], kLevelCatalogName, record, kLevelHeader[6]);
        definition.clearKillCount = ParseUnsigned(
            record.fields[7], kLevelCatalogName, record, kLevelHeader[7]);
        if (definition.activeEnemyLimit == 0) {
            ThrowFieldError(
                kLevelCatalogName,
                record,
                kLevelHeader[6],
                "active enemy limit must be greater than zero");
        }
        if (definition.clearKillCount == 0) {
            ThrowFieldError(
                kLevelCatalogName,
                record,
                kLevelHeader[7],
                "clear kill count must be greater than zero");
        }
        definitions.push_back(std::move(definition));
    }

    std::unordered_map<LevelDefinitionId, std::size_t> indices;
    indices.reserve(definitions.size());
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        indices.emplace(definitions[index].id, index);
    }
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const LevelDefinition& definition = definitions[index];
        if (definition.nextLevelId.has_value() &&
            !indices.contains(*definition.nextLevelId)) {
            ThrowFieldError(
                kLevelCatalogName,
                document.records[index],
                kLevelHeader[3],
                "referenced level ID does not exist");
        }
    }

    std::vector<std::size_t> incomingCounts(definitions.size(), 0);
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        if (!definitions[index].nextLevelId.has_value()) {
            continue;
        }
        const std::size_t targetIndex = indices.at(*definitions[index].nextLevelId);
        ++incomingCounts[targetIndex];
        if (incomingCounts[targetIndex] > 1) {
            ThrowFieldError(
                kLevelCatalogName,
                document.records[index],
                kLevelHeader[3],
                "linear progression cannot merge multiple levels into one destination");
        }
    }

    std::optional<std::size_t> startIndex;
    for (std::size_t index = 0; index < incomingCounts.size(); ++index) {
        if (incomingCounts[index] != 0) {
            continue;
        }
        if (startIndex.has_value()) {
            throw GameDataError("level CSV progression must have exactly one start level");
        }
        startIndex = index;
    }
    if (!startIndex.has_value()) {
        throw GameDataError("level CSV progression contains a cycle or has no start level");
    }

    std::vector<bool> visited(definitions.size(), false);
    std::vector<LevelDefinition> ordered;
    ordered.reserve(definitions.size());
    std::optional<std::size_t> currentIndex = startIndex;
    for (std::size_t step = 0; step < definitions.size(); ++step) {
        if (!currentIndex.has_value()) {
            throw GameDataError(
                "level CSV progression terminates before visiting every level");
        }
        if (visited[*currentIndex]) {
            throw GameDataError("level CSV progression contains a cycle");
        }
        visited[*currentIndex] = true;
        ordered.push_back(definitions[*currentIndex]);
        const std::optional<LevelDefinitionId> next = definitions[*currentIndex].nextLevelId;
        currentIndex = next.has_value()
                           ? std::optional<std::size_t>{indices.at(*next)}
                           : std::nullopt;
    }
    if (currentIndex.has_value()) {
        throw GameDataError("level CSV progression must terminate with an empty next_level_id");
    }
    return ordered;
}

void ValidateResourceRoot(const std::filesystem::path& resourceRoot) {
    std::error_code directoryError;
    if (resourceRoot.empty() ||
        !std::filesystem::is_directory(resourceRoot, directoryError)) {
        std::string error = "game-data resource root is not a directory: " +
                            resourceRoot.generic_string();
        if (directoryError) {
            error += " (" + directoryError.message() + ")";
        }
        throw GameDataError(error);
    }
}

[[nodiscard]] ParsedCatalogData BuildCatalogData(
    const data::CsvDocument& enemies,
    const data::CsvDocument& enemyAnimations,
    const data::CsvDocument& weapons,
    const data::CsvDocument& levels,
    const std::filesystem::path& resourceRoot) {
    ValidateResourceRoot(resourceRoot);
    ParsedCatalogData parsed{
        ParseEnemies(enemies, resourceRoot),
        ParseWeapons(weapons, resourceRoot),
        ParseLevels(levels, resourceRoot),
    };
    ParseEnemyAnimations(enemyAnimations, parsed.enemies);
    return parsed;
}

[[nodiscard]] const data::CsvDocument& RequireDocument(
    const data::CsvParseResult& result,
    const std::string_view label) {
    if (!result.document.has_value()) {
        throw GameDataError(std::string{label} + " CSV: " + result.error);
    }
    return *result.document;
}

} // namespace

const EnemyDefinition* EnemyCatalog::FindById(const std::string_view id) const noexcept {
    const auto found = std::find_if(
        definitions_.begin(), definitions_.end(), [id](const EnemyDefinition& definition) {
            return std::string_view{definition.id} == id;
        });
    return found == definitions_.end() ? nullptr : &*found;
}

const EnemyDefinition* EnemyCatalog::FindByKind(const EnemyKind kind) const noexcept {
    const auto found = std::find_if(
        definitions_.begin(), definitions_.end(), [kind](const EnemyDefinition& definition) {
            return definition.kind == kind;
        });
    return found == definitions_.end() ? nullptr : &*found;
}

const WeaponDefinition* WeaponCatalog::FindById(const std::string_view id) const noexcept {
    const auto found = std::find_if(
        definitions_.begin(), definitions_.end(), [id](const WeaponDefinition& definition) {
            return std::string_view{definition.id} == id;
        });
    return found == definitions_.end() ? nullptr : &*found;
}

const WeaponDefinition* WeaponCatalog::GetDefaultWeapon() const noexcept {
    return definitions_.empty() ? nullptr : &definitions_.front();
}

const LevelDefinition* LevelCatalog::FindById(const std::string_view id) const noexcept {
    const auto found = std::find_if(
        definitions_.begin(), definitions_.end(), [id](const LevelDefinition& definition) {
            return std::string_view{definition.id} == id;
        });
    return found == definitions_.end() ? nullptr : &*found;
}

const LevelDefinition* LevelCatalog::GetStartLevel() const noexcept {
    return definitions_.empty() ? nullptr : &definitions_.front();
}

GameDataLoadResult GameDataLoader::Load(const GameDataPaths& paths) {
    try {
        const data::CsvParseResult enemyResult = data::Csv::Load(paths.enemiesCsvPath);
        const data::CsvParseResult enemyAnimationResult =
            data::Csv::Load(paths.enemyAnimationClipsCsvPath);
        const data::CsvParseResult weaponResult = data::Csv::Load(paths.weaponsCsvPath);
        const data::CsvParseResult levelResult = data::Csv::Load(paths.levelsCsvPath);
        ParsedCatalogData parsed = BuildCatalogData(
            RequireDocument(enemyResult, "enemy"),
            RequireDocument(enemyAnimationResult, "enemy animation"),
            RequireDocument(weaponResult, "weapon"),
            RequireDocument(levelResult, "level"),
            paths.resourceRoot);

        GameDataCatalog catalog;
        catalog.enemies.definitions_ = std::move(parsed.enemies);
        catalog.weapons.definitions_ = std::move(parsed.weapons);
        catalog.levels.definitions_ = std::move(parsed.levels);
        return {std::move(catalog), {}};
    } catch (const std::exception& exception) {
        return {std::nullopt, exception.what()};
    }
}

GameDataLoadResult GameDataLoader::Parse(
    const std::string_view enemiesCsv,
    const std::string_view enemyAnimationClipsCsv,
    const std::string_view weaponsCsv,
    const std::string_view levelsCsv,
    const std::filesystem::path& resourceRoot) {
    try {
        const data::CsvParseResult enemyResult = data::Csv::Parse(enemiesCsv);
        const data::CsvParseResult enemyAnimationResult =
            data::Csv::Parse(enemyAnimationClipsCsv);
        const data::CsvParseResult weaponResult = data::Csv::Parse(weaponsCsv);
        const data::CsvParseResult levelResult = data::Csv::Parse(levelsCsv);
        ParsedCatalogData parsed = BuildCatalogData(
            RequireDocument(enemyResult, "enemy"),
            RequireDocument(enemyAnimationResult, "enemy animation"),
            RequireDocument(weaponResult, "weapon"),
            RequireDocument(levelResult, "level"),
            resourceRoot);

        GameDataCatalog catalog;
        catalog.enemies.definitions_ = std::move(parsed.enemies);
        catalog.weapons.definitions_ = std::move(parsed.weapons);
        catalog.levels.definitions_ = std::move(parsed.levels);
        return {std::move(catalog), {}};
    } catch (const std::exception& exception) {
        return {std::nullopt, exception.what()};
    }
}

} // namespace fps
