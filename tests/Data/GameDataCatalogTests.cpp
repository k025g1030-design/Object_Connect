#include "../TestSupport.hpp"

#include "RetroFPS/Data/Csv.hpp"
#include "RetroFPS/Data/GameData.hpp"

#include <filesystem>
#include <string>
#include <string_view>

#ifndef RETROFPS_TEST_RESOURCE_ROOT
#error RETROFPS_TEST_RESOURCE_ROOT must identify the source resource directory.
#endif

namespace fps::tests {
namespace {

[[nodiscard]] const std::filesystem::path& ResourceRoot() {
    static const std::filesystem::path root{RETROFPS_TEST_RESOURCE_ROOT};
    return root;
}

constexpr std::string_view kValidEnemies =
    "enemy_id,kind,damage,attack_interval_seconds,hp,defense,hitbox_radius,hitbox_height,render_width,render_height,texture_name,frame_width_px,frame_height_px\n"
    "melee_basic,melee,15,0.9,50,5,0.2,0.8,0.973913,0.8,white1x1.png,560,460\n"
    "ranged_basic,ranged,10,1.25,40,0,0.2,1.6,1.230769,1.6,white1x1.png,700,910\n";

constexpr std::string_view kValidEnemyAnimations =
    "enemy_id,state,origin_x_px,origin_y_px,frame_count,seconds_per_frame,event_frame_index,muzzle_x_px,muzzle_y_px\n"
    "melee_basic,idle,0,0,3,0.1,,,\n"
    "melee_basic,move,0,460,4,0.1,,,\n"
    "melee_basic,attack,0,920,6,0.05,3,,\n"
    "melee_basic,dead,0,1380,4,0.1,,,\n"
    "ranged_basic,idle,0,0,3,0.1,,,\n"
    "ranged_basic,move,0,910,4,0.1,,,\n"
    "ranged_basic,attack,0,1820,5,0.05,2,350,420\n"
    "ranged_basic,dead,0,2730,4,0.1,,,\n";

constexpr std::string_view kValidWeapons =
    "weapon_id,damage,magazine_size,reserve_ammo,recoil,automatic,fire_interval_seconds,reload_seconds,texture_name\n"
    "starter_pistol,25,12,48,1.5,false,0.2,1.5,white1x1.png\n";

constexpr std::string_view kValidLevels =
    "level_id,level_name,map_path,next_level_id,ranged_enemy_count,melee_enemy_count,active_enemy_limit,clear_kill_count\n"
    "room_01,ROOM 1,maps/mvp_map.txt,room_02,2,2,2,3\n"
    "room_02,ROOM 2,maps/mvp_map_02.txt,,3,3,3,5\n";

[[nodiscard]] GameDataLoadResult ParseWith(
    const std::string_view enemies,
    const std::string_view enemyAnimations,
    const std::string_view weapons,
    const std::string_view levels) {
    return GameDataLoader::Parse(
        enemies, enemyAnimations, weapons, levels, ResourceRoot());
}

[[nodiscard]] GameDataLoadResult ParseWith(
    const std::string_view enemies,
    const std::string_view weapons,
    const std::string_view levels) {
    return ParseWith(enemies, kValidEnemyAnimations, weapons, levels);
}

void ExpectRejected(
    TestContext& context,
    const GameDataLoadResult& result,
    const std::string_view expectedErrorFragment,
    const std::string_view description) {
    context.Expect(!result.Succeeded(), description);
    context.Expect(!result.catalog.has_value(), "rejected game data has no partial catalog");
    context.Expect(
        result.error.find(expectedErrorFragment) != std::string::npos,
        "rejected game data reports the expected diagnostic category");
}

[[nodiscard]] std::string ReplaceOnce(
    const std::string_view source,
    const std::string_view target,
    const std::string_view replacement) {
    std::string result{source};
    const std::size_t position = result.find(target);
    if (position != std::string::npos) {
        result.replace(position, target.size(), replacement);
    }
    return result;
}

void TestCsvSyntax(TestContext& context) {
    const std::string quoted =
        "\xEF\xBB\xBFname,value\r\n"
        "\"Room, \"\"Alpha\"\"\",42\r\n"
        "\"two\r\nlines\",7\r\n";
    const data::CsvParseResult parsed = data::Csv::Parse(quoted);
    context.Expect(parsed.Succeeded(), "CSV accepts UTF-8 BOM, CRLF, and quoted fields");
    if (parsed.document.has_value()) {
        context.Expect(
            parsed.document->header.size() == 2 && parsed.document->header[0] == "name" &&
                parsed.document->header[1] == "value",
            "CSV strips the leading BOM and preserves the exact header");
        context.Expect(parsed.document->records.size() == 2, "CSV ignores a final line ending");
        if (parsed.document->records.size() == 2) {
            context.Expect(
                parsed.document->records[0].fields[0] == "Room, \"Alpha\"",
                "CSV decodes commas and escaped quotes inside a quoted field");
            context.Expect(
                parsed.document->records[1].fields[0] == "two\nlines",
                "CSV normalizes a quoted CRLF to a newline character");
            context.Expect(
                parsed.document->records[1].lineNumber == 3,
                "CSV records retain their physical starting line");
        }
    }

    const auto expectSyntaxError = [&context](
                                       const std::string_view csv,
                                       const std::string_view description) {
        const data::CsvParseResult result = data::Csv::Parse(csv);
        context.Expect(!result.Succeeded(), description);
        context.Expect(!result.error.empty(), "malformed CSV reports a diagnostic");
    };
    expectSyntaxError("a,b\r1,2", "CSV rejects a lone carriage return");
    expectSyntaxError("a,b\nleft\"quote,2", "CSV rejects quotes inside unquoted fields");
    expectSyntaxError("a,b\n\"closed\"x,2", "CSV rejects characters after a closing quote");
    expectSyntaxError("a,b\n\"open,2", "CSV rejects unterminated quoted fields");
    expectSyntaxError("a,b\n1", "CSV rejects records whose field count differs from the header");
}

void TestValidCatalogAndQueries(TestContext& context) {
    const std::string quotedLevels =
        "\xEF\xBB\xBFlevel_id,level_name,map_path,next_level_id,ranged_enemy_count,melee_enemy_count,active_enemy_limit,clear_kill_count\r\n"
        "room_02,ROOM 2,maps/mvp_map_02.txt,,3,3,3,5\r\n"
        "room_01,\"ROOM, \"\"ONE\"\"\",maps/mvp_map.txt,room_02,2,2,2,3\r\n";
    const GameDataLoadResult result = ParseWith(kValidEnemies, kValidWeapons, quotedLevels);
    context.Expect(result.Succeeded(), "valid game-data catalogs parse together");
    context.Expect(result.error.empty(), "successful game-data parsing clears the error");
    if (!result.catalog.has_value()) {
        return;
    }

    const GameDataCatalog& catalog = *result.catalog;
    context.Expect(catalog.enemies.GetDefinitions().size() == 2, "enemy catalog owns two definitions");
    const EnemyDefinition* const melee = catalog.enemies.FindByKind(EnemyKind::Melee);
    const EnemyDefinition* const ranged = catalog.enemies.FindById("ranged_basic");
    context.Expect(
        melee != nullptr && melee->id == "melee_basic" && NearlyEqual(melee->damage, 15.0f) &&
            NearlyEqual(melee->hitboxRadius, 0.2f) &&
            NearlyEqual(melee->hitboxHeight, 0.8f) &&
            NearlyEqual(melee->renderWidth, 0.973913f) &&
            NearlyEqual(melee->renderHeight, 0.8f) &&
            melee->frameWidthPixels == 560U && melee->frameHeightPixels == 460U &&
            melee->animations.idle.frameCount == 3U &&
            melee->animations.moving.originYpx == 460U &&
            melee->animations.attacking.eventFrameIndex == 3U &&
            !melee->animations.attacking.muzzlePixel.has_value(),
        "enemy catalog queries preserve melee collision, rendering, and animation data");
    context.Expect(
        ranged != nullptr && ranged->kind == EnemyKind::Ranged &&
            NearlyEqual(ranged->maxHealth, 40.0f) &&
            ranged->animations.attacking.frameCount == 5U &&
            NearlyEqual(ranged->animations.attacking.secondsPerFrame, 0.05f) &&
            ranged->animations.attacking.eventFrameIndex == 2U &&
            ranged->animations.attacking.muzzlePixel.has_value() &&
            ranged->animations.attacking.muzzlePixel->x == 350U &&
            ranged->animations.attacking.muzzlePixel->y == 420U &&
            ranged->animations.dead.originYpx == 2730U,
        "enemy catalog finds a ranged definition with its attack event and muzzle data");
    context.Expect(catalog.enemies.FindById("missing") == nullptr, "unknown enemy ID returns null");

    const WeaponDefinition* const weapon = catalog.weapons.GetDefaultWeapon();
    context.Expect(
        weapon != nullptr && weapon == catalog.weapons.FindById("starter_pistol") &&
            weapon->magazineCapacity == 12 && weapon->reserveAmmo == 48 &&
            !weapon->automatic && NearlyEqual(weapon->fireIntervalSeconds, 0.2f) &&
            NearlyEqual(weapon->reloadSeconds, 1.5f),
        "weapon catalog exposes the first row as the default weapon");
    context.Expect(catalog.weapons.FindById("missing") == nullptr, "unknown weapon ID returns null");

    const LevelDefinition* const first = catalog.levels.GetStartLevel();
    const LevelDefinition* const second = catalog.levels.FindById("room_02");
    context.Expect(
        first != nullptr && first->id == "room_01" && first->name == "ROOM, \"ONE\"" &&
            first->nextLevelId.has_value() && *first->nextLevelId == "room_02" &&
            first->rangedEnemyCount == 2 && first->meleeEnemyCount == 2 &&
            first->activeEnemyLimit == 2 && first->clearKillCount == 3,
        "level graph resolves its unique start and preserves quoted display text");
    context.Expect(
        second != nullptr && !second->nextLevelId.has_value() &&
            second->mapPath.generic_string() == "maps/mvp_map_02.txt",
        "empty next level ID marks the Results destination");
    context.Expect(catalog.levels.FindById("missing") == nullptr, "unknown level ID returns null");
}

void TestFileLoading(TestContext& context) {
    GameDataPaths paths;
    paths.enemiesCsvPath = ResourceRoot() / "data" / "enemies.csv";
    paths.enemyAnimationClipsCsvPath =
        ResourceRoot() / "data" / "enemy_animation_clips.csv";
    paths.weaponsCsvPath = ResourceRoot() / "data" / "weapons.csv";
    paths.levelsCsvPath = ResourceRoot() / "data" / "levels.csv";
    paths.resourceRoot = ResourceRoot();
    const GameDataLoadResult loaded = GameDataLoader::Load(paths);
    context.Expect(loaded.Succeeded(), "source game-data CSV files load through filesystem paths");
    if (loaded.catalog.has_value()) {
        context.Expect(
            loaded.catalog->levels.GetDefinitions().size() == 2,
            "deployed catalog fixture defines the two-map campaign");
    }

    paths.levelsCsvPath = ResourceRoot() / "data" / "missing.csv";
    const GameDataLoadResult missing = GameDataLoader::Load(paths);
    ExpectRejected(context, missing, "failed to open", "missing catalog file is rejected");

    paths.levelsCsvPath = ResourceRoot() / "data" / "levels.csv";
    paths.enemyAnimationClipsCsvPath = ResourceRoot() / "data" / "missing.csv";
    const GameDataLoadResult missingAnimations = GameDataLoader::Load(paths);
    ExpectRejected(
        context,
        missingAnimations,
        "failed to open",
        "missing enemy animation catalog file is rejected");
}

void TestExactHeadersAndScalarValidation(TestContext& context) {
    const std::string wrongHeader =
        "enemy_id,kind,damage,attack_interval_seconds,health,defense,hitbox_radius,hitbox_height,render_width,render_height,texture_name,frame_width_px,frame_height_px\n"
        "melee_basic,melee,15,0.9,50,5,0.2,0.8,0.97,0.8,white1x1.png,560,460\n"
        "ranged_basic,ranged,10,1.25,40,0,0.2,1.6,1.23,1.6,white1x1.png,700,910\n";
    ExpectRejected(
        context,
        ParseWith(wrongHeader, kValidWeapons, kValidLevels),
        "header must be exactly",
        "renamed enemy header is rejected");

    const std::string nonFiniteEnemy =
        "enemy_id,kind,damage,attack_interval_seconds,hp,defense,hitbox_radius,hitbox_height,render_width,render_height,texture_name,frame_width_px,frame_height_px\n"
        "melee_basic,melee,nan,0.9,50,5,0.2,0.8,0.97,0.8,white1x1.png,560,460\n"
        "ranged_basic,ranged,10,1.25,40,0,0.2,1.6,1.23,1.6,white1x1.png,700,910\n";
    const GameDataLoadResult nonFiniteResult =
        ParseWith(nonFiniteEnemy, kValidWeapons, kValidLevels);
    ExpectRejected(
        context,
        nonFiniteResult,
        "finite decimal",
        "non-finite enemy damage is rejected");
    context.Expect(
        nonFiniteResult.error.find("enemies.csv line 2, column 3, field 'damage'") !=
            std::string::npos,
        "semantic CSV diagnostics include filename, line, column, and field");

    const std::string zeroHitbox =
        "enemy_id,kind,damage,attack_interval_seconds,hp,defense,hitbox_radius,hitbox_height,render_width,render_height,texture_name,frame_width_px,frame_height_px\n"
        "melee_basic,melee,15,0.9,50,5,0,0.8,0.97,0.8,white1x1.png,560,460\n"
        "ranged_basic,ranged,10,1.25,40,0,0.2,1.6,1.23,1.6,white1x1.png,700,910\n";
    ExpectRejected(
        context,
        ParseWith(zeroHitbox, kValidWeapons, kValidLevels),
        "greater than zero",
        "non-positive hitbox radius is rejected");

    const std::string zeroRenderWidth = ReplaceOnce(
        kValidEnemies,
        "0.2,0.8,0.973913,0.8",
        "0.2,0.8,0,0.8");
    ExpectRejected(
        context,
        ParseWith(zeroRenderWidth, kValidWeapons, kValidLevels),
        "greater than zero",
        "non-positive enemy render size is rejected");

    const std::string zeroFrameWidth = ReplaceOnce(
        kValidEnemies,
        "white1x1.png,560,460",
        "white1x1.png,0,460");
    ExpectRejected(
        context,
        ParseWith(zeroFrameWidth, kValidWeapons, kValidLevels),
        "greater than zero",
        "non-positive atlas frame size is rejected");

    const std::string negativeMagazine =
        "weapon_id,damage,magazine_size,reserve_ammo,recoil,automatic,fire_interval_seconds,reload_seconds,texture_name\n"
        "starter_pistol,25,-1,48,1.5,false,0.2,1.5,white1x1.png\n";
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, negativeMagazine, kValidLevels),
        "unsigned 32-bit integer",
        "negative ammunition count is rejected");

    const std::string looseBoolean =
        "weapon_id,damage,magazine_size,reserve_ammo,recoil,automatic,fire_interval_seconds,reload_seconds,texture_name\n"
        "starter_pistol,25,12,48,1.5,True,0.2,1.5,white1x1.png\n";
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, looseBoolean, kValidLevels),
        "exactly 'true' or 'false'",
        "weapon automatic flag is case-sensitive and strict");

    const std::string overflowCount =
        "level_id,level_name,map_path,next_level_id,ranged_enemy_count,melee_enemy_count,active_enemy_limit,clear_kill_count\n"
        "room_01,ROOM 1,maps/mvp_map.txt,,4294967296,1,1,1\n";
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, kValidWeapons, overflowCount),
        "unsigned 32-bit integer",
        "level enemy counts reject unsigned overflow");
}

void TestIdsPathsAndLinearProgression(TestContext& context) {
    const std::string duplicateEnemy =
        "enemy_id,kind,damage,attack_interval_seconds,hp,defense,hitbox_radius,hitbox_height,render_width,render_height,texture_name,frame_width_px,frame_height_px\n"
        "shared,melee,15,0.9,50,5,0.2,0.8,0.97,0.8,white1x1.png,560,460\n"
        "shared,ranged,10,1.25,40,0,0.2,1.6,1.23,1.6,white1x1.png,700,910\n";
    ExpectRejected(
        context,
        ParseWith(duplicateEnemy, kValidWeapons, kValidLevels),
        "duplicate enemy ID",
        "duplicate enemy definition ID is rejected");

    const std::string duplicateKind =
        "enemy_id,kind,damage,attack_interval_seconds,hp,defense,hitbox_radius,hitbox_height,render_width,render_height,texture_name,frame_width_px,frame_height_px\n"
        "melee_basic,melee,15,0.9,50,5,0.2,0.8,0.97,0.8,white1x1.png,560,460\n"
        "melee_other,melee,10,1.25,40,0,0.2,1.6,1.23,1.6,white1x1.png,700,910\n";
    ExpectRejected(
        context,
        ParseWith(duplicateKind, kValidWeapons, kValidLevels),
        "only one melee",
        "ambiguous melee marker binding is rejected");

    const std::string invalidId =
        "weapon_id,damage,magazine_size,reserve_ammo,recoil,automatic,fire_interval_seconds,reload_seconds,texture_name\n"
        "Starter-Pistol,25,12,48,1.5,false,0.2,1.5,white1x1.png\n";
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, invalidId, kValidLevels),
        "lower_snake_case",
        "definition IDs use a stable lower-snake-case format");

    const std::string parentPath =
        "enemy_id,kind,damage,attack_interval_seconds,hp,defense,hitbox_radius,hitbox_height,render_width,render_height,texture_name,frame_width_px,frame_height_px\n"
        "melee_basic,melee,15,0.9,50,5,0.2,0.8,0.97,0.8,../white1x1.png,560,460\n"
        "ranged_basic,ranged,10,1.25,40,0,0.2,1.6,1.23,1.6,white1x1.png,700,910\n";
    ExpectRejected(
        context,
        ParseWith(parentPath, kValidWeapons, kValidLevels),
        "parent-directory",
        "resource path cannot escape the configured root");

    const std::string missingPath =
        "weapon_id,damage,magazine_size,reserve_ammo,recoil,automatic,fire_interval_seconds,reload_seconds,texture_name\n"
        "starter_pistol,25,12,48,1.5,false,0.2,1.5,missing.png\n";
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, missingPath, kValidLevels),
        "does not exist",
        "referenced texture must exist under the resource root");

    const std::string missingNext =
        "level_id,level_name,map_path,next_level_id,ranged_enemy_count,melee_enemy_count,active_enemy_limit,clear_kill_count\n"
        "room_01,ROOM 1,maps/mvp_map.txt,missing_room,2,2,2,3\n";
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, kValidWeapons, missingNext),
        "referenced level ID does not exist",
        "next level ID must resolve inside the catalog");

    const std::string cycle =
        "level_id,level_name,map_path,next_level_id,ranged_enemy_count,melee_enemy_count,active_enemy_limit,clear_kill_count\n"
        "room_01,ROOM 1,maps/mvp_map.txt,room_02,2,2,2,3\n"
        "room_02,ROOM 2,maps/mvp_map_02.txt,room_01,3,3,3,5\n";
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, kValidWeapons, cycle),
        "cycle",
        "linear campaign cannot end in a cycle");

    const std::string disconnected =
        "level_id,level_name,map_path,next_level_id,ranged_enemy_count,melee_enemy_count,active_enemy_limit,clear_kill_count\n"
        "room_01,ROOM 1,maps/mvp_map.txt,,2,2,2,3\n"
        "room_02,ROOM 2,maps/mvp_map_02.txt,,3,3,3,5\n";
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, kValidWeapons, disconnected),
        "exactly one start level",
        "every level row must belong to one connected linear campaign");

    const std::string excessiveActiveLimit =
        "level_id,level_name,map_path,next_level_id,ranged_enemy_count,melee_enemy_count,active_enemy_limit,clear_kill_count\n"
        "room_01,ROOM 1,maps/mvp_map.txt,,2,2,5,3\n";
    context.Expect(
        ParseWith(kValidEnemies, kValidWeapons, excessiveActiveLimit).Succeeded(),
        "active enemy limit is independent from the configured spawn total");

    const std::string excessiveClearCount =
        "level_id,level_name,map_path,next_level_id,ranged_enemy_count,melee_enemy_count,active_enemy_limit,clear_kill_count\n"
        "room_01,ROOM 1,maps/mvp_map.txt,,2,2,2,5\n";
    context.Expect(
        ParseWith(kValidEnemies, kValidWeapons, excessiveClearCount).Succeeded(),
        "door-clear kill count is independent from the configured spawn total");

    const std::string impossibleButWellFormed =
        "level_id,level_name,map_path,next_level_id,ranged_enemy_count,melee_enemy_count,active_enemy_limit,clear_kill_count\n"
        "room_01,ROOM 1,maps/mvp_map.txt,,0,0,1,1\n";
    context.Expect(
        ParseWith(kValidEnemies, kValidWeapons, impossibleButWellFormed).Succeeded(),
        "well-formed but unwinnable spawn/clear relationships remain a level-data responsibility");
}

void TestEnemyAnimationValidation(TestContext& context) {
    const std::string wrongHeader = ReplaceOnce(
        kValidEnemyAnimations, "origin_x_px", "origin_x");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, wrongHeader, kValidWeapons, kValidLevels),
        "header must be exactly",
        "renamed animation header is rejected");

    const std::string unknownEnemy = ReplaceOnce(
        kValidEnemyAnimations, "melee_basic,idle", "missing_enemy,idle");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, unknownEnemy, kValidWeapons, kValidLevels),
        "referenced enemy ID does not exist",
        "animation rows cannot reference an unknown enemy");

    const std::string unknownState = ReplaceOnce(
        kValidEnemyAnimations, "melee_basic,idle", "melee_basic,walk");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, unknownState, kValidWeapons, kValidLevels),
        "expected exactly 'idle', 'move', 'attack', or 'dead'",
        "animation state names are strict");

    const std::string duplicateState = ReplaceOnce(
        kValidEnemyAnimations,
        "melee_basic,move,0,460,4,0.1,,,",
        "melee_basic,idle,0,460,4,0.1,,,");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, duplicateState, kValidWeapons, kValidLevels),
        "duplicate animation state",
        "each enemy animation state may appear only once");

    const std::string missingState = ReplaceOnce(
        kValidEnemyAnimations, "melee_basic,dead,0,1380,4,0.1,,,\n", "");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, missingState, kValidWeapons, kValidLevels),
        "missing state 'dead' for enemy 'melee_basic'",
        "every enemy requires all four animation states");

    const std::string zeroFrameCount = ReplaceOnce(
        kValidEnemyAnimations,
        "melee_basic,idle,0,0,3,0.1,,,",
        "melee_basic,idle,0,0,0,0.1,,,");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, zeroFrameCount, kValidWeapons, kValidLevels),
        "greater than zero",
        "animation frame count must be positive");

    const std::string zeroFrameTime = ReplaceOnce(
        kValidEnemyAnimations,
        "melee_basic,idle,0,0,3,0.1,,,",
        "melee_basic,idle,0,0,3,0,,,");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, zeroFrameTime, kValidWeapons, kValidLevels),
        "greater than zero",
        "animation frame duration must be positive");

    const std::string missingAttackEvent = ReplaceOnce(
        kValidEnemyAnimations,
        "melee_basic,attack,0,920,6,0.05,3,,",
        "melee_basic,attack,0,920,6,0.05,,,");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, missingAttackEvent, kValidWeapons, kValidLevels),
        "requires an event frame index",
        "attack animations require an event frame");

    const std::string outOfRangeAttackEvent = ReplaceOnce(
        kValidEnemyAnimations,
        "melee_basic,attack,0,920,6,0.05,3,,",
        "melee_basic,attack,0,920,6,0.05,6,,");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, outOfRangeAttackEvent, kValidWeapons, kValidLevels),
        "less than frame count",
        "attack event frame must address an existing frame");

    const std::string nonAttackEvent = ReplaceOnce(
        kValidEnemyAnimations,
        "melee_basic,idle,0,0,3,0.1,,,",
        "melee_basic,idle,0,0,3,0.1,0,,");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, nonAttackEvent, kValidWeapons, kValidLevels),
        "must be empty for non-attack",
        "non-attack animations cannot define attack events");

    const std::string missingRangedMuzzle = ReplaceOnce(
        kValidEnemyAnimations,
        "ranged_basic,attack,0,1820,5,0.05,2,350,420",
        "ranged_basic,attack,0,1820,5,0.05,2,350,");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, missingRangedMuzzle, kValidWeapons, kValidLevels),
        "requires both muzzle pixel coordinates",
        "ranged attacks require a complete muzzle point");

    const std::string outsideRangedMuzzle = ReplaceOnce(
        kValidEnemyAnimations,
        "ranged_basic,attack,0,1820,5,0.05,2,350,420",
        "ranged_basic,attack,0,1820,5,0.05,2,700,420");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, outsideRangedMuzzle, kValidWeapons, kValidLevels),
        "must be inside one animation frame",
        "ranged muzzle coordinates must lie inside a frame");

    const std::string unexpectedMuzzle = ReplaceOnce(
        kValidEnemyAnimations,
        "melee_basic,attack,0,920,6,0.05,3,,",
        "melee_basic,attack,0,920,6,0.05,3,10,20");
    ExpectRejected(
        context,
        ParseWith(kValidEnemies, unexpectedMuzzle, kValidWeapons, kValidLevels),
        "must be empty except for ranged attack",
        "melee and non-attack clips cannot define a muzzle point");

    const std::string overflowingEnemies = ReplaceOnce(
        kValidEnemies,
        "white1x1.png,560,460",
        "white1x1.png,4294967295,460");
    ExpectRejected(
        context,
        ParseWith(
            overflowingEnemies,
            kValidEnemyAnimations,
            kValidWeapons,
            kValidLevels),
        "atlas rectangle arithmetic overflows",
        "atlas rectangle arithmetic is checked in a widened integer domain");
}

} // namespace

void RunGameDataCatalogTests(TestContext& context) {
    TestCsvSyntax(context);
    TestValidCatalogAndQueries(context);
    TestFileLoading(context);
    TestExactHeadersAndScalarValidation(context);
    TestIdsPathsAndLinearProgression(context);
    TestEnemyAnimationValidation(context);
}

} // namespace fps::tests
