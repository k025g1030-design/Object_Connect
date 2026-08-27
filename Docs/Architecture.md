# Object_FPS v2 Architecture

## 目標與邊界

目前架構提供一個完整但刻意精簡的 FPS 基礎流程：CSV 資料載入、線性房間 campaign、動態敵人生成、
玩家射擊／換彈、近戰與遠程敵人傷害、kill-gated door，以及成功或死亡的 Results 統計。

公開 API 位於 `include/RetroFPS/`，私有實作位於 `src/`。公開 contract 優先使用
`fps::Float2`／`Float3`、settings、snapshot 與事件；KamataEngine／Win32 只出現在
Application、Input 與 Rendering adapter 邊界。`src/` 內的 header 不保證對外穩定。

Gameplay ground 仍固定在 `Y = 0`，玩家移動是 XZ 圓形碰撞；combat 另外以包含高度的垂直 capsule
處理命中。現階段不包含跳躍、樓梯、高低差、多樓層、武器切換、拾取物、reload animation 或新音效。

## 模組責任

| 模組 | 責任 | 不負責 |
| --- | --- | --- |
| Core | `Application` 的 engine lifetime、主迴圈、工作目錄、錯誤顯示；`FrameTimer` 的 delta clamp | Map、戰鬥規則、繪製內容 |
| Data | 嚴格 CSV parse；enemy／animation clip／weapon／level definition catalog 與交叉驗證 | Runtime 狀態、AI、Rendering |
| Game | `GameFlow`、`MapSceneManager`、`CampaignRunState`、level progression 與各 subsystem 協調 | 平台初始化、底層碰撞演算法、asset renderer 實作 |
| Math | Engine-independent `Float2`／`Float3` value type | Matrix／render pipeline 或 gameplay 規則 |
| Input | 取樣實體鍵、mouse、focus；依 Game 請求管理 cursor capture | Menu、射擊、reload、移動等遊戲語意 |
| World | enum `GridMap`、marker 座標、`WorldSettings` 與 active `World` owner | 檔案 I/O、Rendering、collision resolution |
| GridMapLoader | Map file／text 到已驗證 `GridMap` 的 I/O 與 parse error | 保存 active world、產生 mesh |
| Collision | XZ circle movement；ray／segment 對 wall、floor、垂直 capsule 的最近 hit | Renderer mesh、完整 3D physics |
| Gameplay/Player | `Player`、`PlayerCombatState`、設定、movement／look controller 與 recoil view offset | KamataEngine Camera、檔案 I/O、HUD |
| Gameplay/Weapon | 彈匣／備彈、semi／automatic trigger、cooldown、R-only reload、`ShotEvent` 與 HUD snapshot | Raycast、enemy damage、圖像繪製 |
| Gameplay/Enemy | 動態 `EnemySystem`、damage／death／hit flash、四狀態 AI、A*／LOS、attack event；`EnemySpawnDirector` | KamataEngine、Player HP owner、projectile simulation |
| Gameplay/Combat | 白色 visual tracer 與橙色 authoritative enemy projectile 的生命週期／swept hit | 發射決策、HP／kill 統計、Rendering |
| Rendering | camera、camera-centered sky、map／door、Atlas enemy billboard、projectile、world post-process、weapon／HUD、ASCII UI 與 fade | Gameplay 狀態所有權、AI、damage 計算、transition timing |

`Game` 是唯一高層組裝位置。Data 與 Gameplay 不依賴 KamataEngine；Renderer 只讀 snapshot 或 view model，
不回寫 Player／Enemy／Weapon 狀態。

## 資料契約

執行時由 `Resources/data/` 載入四份 catalog；原本由 `NoviceResources/data/` 部署。

```text
enemies.csv -> EnemyDefinition
  enemy_id, kind, damage, attack_interval_seconds, hp, defense,
  hitbox_radius, hitbox_height, render_width, render_height,
  texture_name, frame_width_px, frame_height_px

enemy_animation_clips.csv -> EnemyAnimationSetDefinition
  enemy_id, state, origin_x_px, origin_y_px, frame_count,
  seconds_per_frame, event_frame_index, muzzle_x_px, muzzle_y_px

weapons.csv -> WeaponDefinition
  weapon_id, damage, magazine_size, reserve_ammo, recoil, automatic,
  fire_interval_seconds, reload_seconds, texture_name

levels.csv -> LevelDefinition
  level_id, level_name, map_path, next_level_id, ranged_enemy_count,
  melee_enemy_count, active_enemy_limit, clear_kill_count
```

`GameConfig` 保存四份資料表路徑、resource root、`startLevelId`、`startingWeaponId` 與近／遠敵人
definition ID；地圖進行順序不再由程式內的 path list 決定。`next_level_id` 空白代表最後房間成功出口。

`hitbox_radius`／`hitbox_height` 只屬於 gameplay collision；`render_width`／`render_height` 只屬於
billboard。render size 定義透明留白在內的共用 frame canvas 世界尺寸，而不是每個 frame 的 alpha bounding
box。所有 state 保留同一 canvas 與 ground anchor，避免 idle／move／attack／dead 因留白不同而逐幀縮放或跳動。

CSV reader 支援 UTF-8 BOM、LF／CRLF、quoted field 與 doubled-quote escape。載入器驗證 header、必要欄位、
型別與範圍、duplicate ID、enemy kind、unknown reference、絕對路徑／`..`、缺失 map／texture，以及
從起始 level 出發的線性引用與 cycle。錯誤會附帶來源檔、row、column／field。生成總額、同時上限與
通關 kill quota 是獨立設計參數；程式不替資料作者調整三者的關係。

## 依賴方向

```text
main
  -> Core::Application
      -> Game
          +-> Data::GameDataCatalog
          +-> GameFlow / MapSceneManager / CampaignRunState
          +-> Input
          +-> World
          +-> Gameplay
          |    +-> Player / Weapon / Enemy / Combat
          |    +-> Collision / Math
          +-> Rendering adapters

Gameplay/PlayerController -> InputState + World + Collision + Math
Gameplay/WeaponController -> WeaponDefinition + InputState
Gameplay/EnemySystem      -> EnemyDefinition + World + Collision + Math
EnemySpawnDirector        -> LevelDefinition + GridMap markers + EnemySystem
ProjectileSystem          -> CombatCollision + GridMap + Math
CombatCollision           -> World + Math
Rendering                 -> read-only snapshots + KamataEngine
World                     -> Math
```

下層模組不反向 include `Game`，World／Collision 不 include Rendering。`EnemySystem` 產生資料化 attack
event；Game 將 melee event 套用至 `PlayerCombatState`，或將 ranged event 交給 `ProjectileSystem`。

## Map 與資料的下游

```text
levels.csv
  -> LevelDefinition --------------------+--------------------------+
                                         |                          |
map_path -> GridMapLoader -> GridMap -> World                 CampaignRunState
                              |          |                          |
                              |          +-> PlayerController      +-> Results rows
                              |          +-> CombatCollision
                              |          +-> EnemySystem AI
                              |
                              +-> M/R marker -> EnemySpawnDirector -> dynamic EnemySystem
                              +-> D marker   -> kill-gated exit
                              +-> MapGeometryGenerator -> MapRenderer
```

碰撞直接查詢 `GridMap::IsSolid`，不碰撞 `Object3d`、OBJ 或 `MapGeometry`。door 是可穿越的 presentation
marker；達成 `clear_kill_count` 前，`MapRenderer` 不畫 door，Game 也不接受出口判定。若解鎖當下玩家已在
`D` cell，必須先離開再重新進入，避免 door 出現瞬間自動轉場。

## Ownership 與生命週期

- `main` 在 stack 建立 `Application` 與 `Game`；RAII guard 保證先 `Game::Finalize()`，再
  `KamataEngine::Finalize()`。
- `Game` 以 `std::unique_ptr<Impl>` 隱藏組裝細節。初始化先在暫時的 `Impl` 完成全部 preflight，成功後才提交。
- `GameDataCatalog`、ordered level chain、`PlayerCombatState`、`WeaponController`／`WeaponState` 與
  `CampaignRunState` 位於 `LevelSession` 外。HP、彈匣、備彈、reload 與 cooldown 因此可跨房保留。
- `LevelSession` 只擁有房間內狀態：`World`、`Player`、`EnemySystem`、`EnemySpawnDirector`、
  `ProjectileSystem`、map／enemy／projectile renderer 與 door entry latch。
- 換房會清除 tracer／enemy projectile 與房間 renderer instance；純視覺 weapon kick／camera recoil 清零。
  新遊戲才重置 HP、武器彈藥、campaign kills、生成隊列與結果。
- `CampaignRunState` 保存所有房間定義，但 Results 只投影 `visited` room 的實際 lethal-hit kill 數。
- `MapSceneManager` 保存 active map index、pending destination、fade phase 與 commit barrier。
  Results commit 會銷毀 active `LevelSession`，但保留 campaign 統計供 UI 顯示。
- Renderer 使用 PIMPL 隔離 engine type；dynamic enemy／projectile renderer 以 runtime ID 同步 instance，
  retirement 後移除對應 object。
- 所有 ownership 使用 value、`optional` 或 `unique_ptr`；沒有裸 owning `new/delete`。

## 初始化流程

```text
WinMain
  -> Application::Run(Game)
  -> working directory = executable directory
  -> KamataEngine::Initialize
  -> Game::Initialize(GameConfig)
       1. GameDataLoader 載入並驗證四份 CSV catalog
       2. 從 startLevelId 追蹤 next_level_id，建立 ordered level chain
       3. 載入每張 GridMap，交由 MapSceneManager 驗證並保存
       4. Configure Player／Weapon／Projectile；Initialize CampaignRunState
       5. 對每張 map preflight spawn marker、EnemySpawnDirector、collision 與 CPU geometry
       6. Initialize Camera、sky、scene post-process、map／enemy／projectile／HUD／UI／fade／Input adapter
       7. 建立第一個 LevelSession；GameFlow 設為 MainMenu，cursor capture 關閉
  -> frame loop
```

設定驗證留在擁有語意的 subsystem：Data loader 驗證資料與資源引用，World 驗證 cell size／wall height，
Player controller 驗證 movement／look，Weapon／Projectile controller 驗證 timing／speed，camera 驗證
FOV／clip，renderer 驗證 model／texture。

## Gameplay 每幀流程

只有 stable `Playing` frame 推進 simulation。Paused、fade-out、opaque commit 與 fade-in 期間不推進 AI、
projectile、reload、cooldown 或 recoil recovery。

```text
InputSystem::Sample
  -> GameFlow / focus-loss / pause / transition lock
  -> PlayerController movement + mouse look
  -> WeaponController trigger / cooldown / reload
  -> consume ShotEvent
       camera-center ray -> nearest wall/floor/enemy capsule (max 50m)
       muzzle-to-aim segment -> obstruction correction
       immediate enemy damage + optional room kill
       spawn cosmetic white tracer
  -> EnemySystem AI update
       melee event  -> immediate PlayerCombatState damage
       ranged event -> spawn orange projectile toward fire-time player center
  -> ProjectileSystem swept segment update
       wall hit -> retire
       player capsule hit -> damage once, retire
  -> retire expired dead enemies / refill active slots
  -> update kill-gated door and exit-entry latch
  -> sync snapshots to enemy/projectile renderer and camera
```

玩家 hitscan 先按射擊前的準星方向解決，之後才把 CSV 的 recoil 角度加到視角。命中無穿透、爆頭、
random spread 或 friendly fire。damage 公式為 `max(1, weaponDamage - enemyDefense)`；有效 hit 讓敵人以
約 1.5 倍亮度閃爍 0.12 秒。死亡敵人立刻退出 active count、damage 與 collision，但保留 dead clip 的
0.4 秒並持續占用 spawn 位置，播放完才退休。

白色玩家球只呈現 muzzle 到實際 hit point 的曳光，權威命中不等待球抵達。橙色敵彈以固定方向飛行，
不追蹤玩家，使用 swept segment／expanded capsule 防止高速穿透；撞牆、命中玩家或超時後移除。

`EnemySpawnDirector` 從 melee 開始交錯消耗近／遠額度，某一類耗盡後繼續另一類。每類 marker 依 map
row-major 順序 round-robin 重用，同時只計存活敵人以限制 active count。marker 與玩家、alive／retiring
enemy 或牆重疊時跳過；同類 marker 全不安全時保留額度，留到後續 frame 重試。每個 gameplay update
結尾即時補位，新生成敵人下一 frame 才執行 AI。即使 door 已解鎖，尚未消耗的生成額度仍正常補生。

## Rendering 與 HUD

world draw 與 composite 順序固定為：

```text
DirectX PreDraw
  -> offscreen sRGB scene + private depth
  -> Sky -> Map -> Enemy billboard -> Projectile sphere
  -> Brightness/Gamma full-screen composite to sRGB backbuffer
  -> Weapon/HUD -> Pause UI -> Crosshair/Text -> Screen fade
  -> DirectX PostDraw
```

sky sphere 半徑 50m，球心每 frame 等於 camera position，但不跟隨 yaw／pitch；它沒有 collision、使用
front-face culling 與 read-only depth，因此不會成為可走出的地圖邊界，也不會遮住之後繪製的遠距地圖物件。
Enemy atlas 的 UV offset 依 KamataEngine `Material::ConstBufferData` 的連續 `Vector3` ABI 配置在 `c3.w/c4.xy`；
若錯誤地把整個 offset 宣告在 `c4`，GPU 會遺失 Y row offset，使所有 runtime state 取到錯誤的 atlas 列。
world material 使用白色 unlit 基底。offscreen 的 sRGB SRV 取樣會先解碼至 linear，shader 乘上
`Brightness=1.25`，再套用 `pow(color, 2.2 / Gamma)`；`Gamma=2.2` 為中性，最後由 sRGB backbuffer
執行唯一一次 linear-to-sRGB encode。Weapon／HUD／UI／文字／fade 在 composite 後繪製，不受此調整。

weapon 使用 CSV texture；目前 `white1x1.png` 染綠，固定由右下角向畫面中央延伸。HUD 在 1280×720
座標系繪製中心四線 crosshair、射擊擴張／回復、右下 `HP` 與視覺上的 `【magazine/reserve】`，以及
crosshair 下方的 `RELOADING` progress bar。Projectile renderer 將 tracer 畫成白球，enemy bullet 畫成橙球。

成功 Results 顯示 `MISSION COMPLETE`，死亡顯示 `GAME OVER`；兩者只列本輪已到達房間的名稱與單一
kill 數，不顯示 quota 分母。回到 Main Menu 再開始才建立全新的 run state。

## Extension points

- **多武器**：catalog 與 ID 已支援多筆 definition；可在 Game 上層新增 inventory／selection，保持
  `WeaponController` 不直接呼叫 engine input 或 renderer。
- **敵人種類**：加入新的 definition 與 AI strategy 時，維持 `EnemySnapshot`／`EnemyAttackEvent` 邊界，
  不讓 renderer 或 projectile system 擁有 AI state。
- **Hit zone**：目前 capsule 是單一 damage zone。若加入 head／limb，擴充 `CombatTarget` 與 hit result，
  不 raycast render mesh。
- **Enemy animation**：使用 `EnemyState + stateElapsedSeconds` 選擇 CSV clip；idle／move loop，attack／dead
  clamp。新增素材必須提供完整四狀態與合法 Atlas rectangle，不修改 AI strategy。
- **非線性 campaign**：目前 loader 與 `CampaignRunState` 假設一條 next-ID chain；branching 必須先明確
  定義 Results、visited room 與回訪統計語意。

## Non-goals

目前不導入：

- ECS、scene graph framework、dependency injection framework
- Full 3D physics、terrain、樓梯、跳躍、高低差或多樓層
- Reflection、serialization framework、script VM、plugin system
- Generic asset pipeline、job system 或 multiplayer architecture
- 補血／補彈拾取、武器切換、reload animation、爆頭、穿透或新音效

`NoviceResources/` 是 KamataEngine 的既有資源來源，build 後同步為 `Resources/`；在沒有同步修改
engine resource lookup 前，不改名或另建平行 asset pipeline。

## Remaining risks

- Headless contract tests 無法證明 executable 啟動與實際 GPU 畫面。sky seam／極點、Atlas 透明邊緣與 muzzle、
  hit flash、Gamma、HUD layering、door、Pause overlay、Results 排版與整體手感仍需人工 runtime regression。
- 每個 build configuration 都必須分別執行 MSVC `/W4 /WX /sdl /permissive-` build 與 CTest；
  尚未完成的 configuration 不視為已驗證。
- Rendering adapter 仍受 KamataEngine ABI、Windows 與 resource-root 規則約束；尚未具備跨平台 backend。
- `MapRenderer` 目前為每個 surface 建立一個 Object3d；更大的 map 可能需要 batching／instancing。
- `GridCollision::MoveCircle` 採固定 X 後 Z 的 axis resolution；未來不同 movement shape 應新增 query，
  而不是擴張 player-specific function。
- static map geometry 只在進入 level 時建立；runtime tile mutation／reload 尚未設計。

本文件描述程式碼責任與已知驗證邊界；不把 headless contract tests 延伸解讀為完整 gameplay／visual regression。
