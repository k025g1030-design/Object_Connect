# Object_FPS v2 Architecture

## 目標與邊界

v2 架構的首要條件是保留目前 MVP 行為，並在相同邊界內支援開始／說明／暫停狀態、
順序式多地圖、enum tile 與出口。WASD、mouse look 和 XZ wall collision 仍不被移入
Gameplay 行為移入 Renderer、引擎 adapter 或通用 framework。

公開 API 位於 `include/RetroFPS/`，私有實作位於 `src/`。公開 contract 優先
使用 `fps::Float2`／`Float3`、settings 與資料結果；KamataEngine／Win32 只在
Application、Input 與 Rendering 邊界出現。`src/` 內的 header 不保證對外穩定。

## 模組責任

| 模組 | 責任 | 不負責 |
| --- | --- | --- |
| Core | `Application` 的 engine lifetime、主迴圈、工作目錄與錯誤顯示；`FrameTimer` 的 delta clamp | Map、玩家規則、繪製內容 |
| Game | `GameFlow` 狀態機、順序式 map progression、`LevelSession` 與 subsystem 協調 | 平台初始化細節、碰撞演算法、資源實作 |
| Math | Engine-independent `Float2`／`Float3` value types | Matrix/render pipeline 或 Gameplay 規則 |
| Input | 取樣實體鍵、mouse、focus；依 Game 請求管理 cursor capture | Menu、`MoveForward`、`RotateCamera` 等遊戲語意 |
| World | enum `GridMap`、marker 座標、`WorldSettings` 與 active `World` owner | 檔案 I/O、Rendering、Collision resolution |
| GridMapLoader | Map file/text 到已驗證 `GridMap` 的 I/O 與 parse error | 保存目前世界、產生 mesh |
| Collision | 以 `GridMap` 回答 circle overlap 與 XZ movement resolution | Renderer mesh、完整 3D physics |
| Gameplay/Player | `Player` 純狀態；`PlayerController` 驗證設定、spawn、解讀 raw input、look 與 movement | KamataEngine Camera、Input API、檔案 I/O、Rendering resources |
| Rendering | map/白門 geometry、3D renderer、ASCII `GameUiRenderer` 與 camera adapter | Gameplay collision、Player 狀態所有權、menu transition |

`Player` 只保存 `positionXZ`、yaw 與 pitch。速度、碰撞半徑、mouse sensitivity、
eye height 與 pitch limit 屬於 `PlayerSettings`；相機 FOV/clip 則屬於
`CameraSettings`。

## 依賴方向

```text
main
  -> Core::Application
      -> Game
          -> GameFlow
          -> Input
          -> World
          -> Gameplay/Player
          -> Rendering

Gameplay/PlayerController -> InputState + World + Collision + Math
Collision                 -> World + Math
Rendering geometry        -> World + Math
World                     -> Math

Application / InputSystem / Rendering adapters -> KamataEngine or Win32
```

下層模組不反向 include `Game`，World/Collision 不 include Rendering，Renderer
也不讀取或更新 Player。`Game` 是唯一高層組裝位置。

### Map 資料的兩條下游

```text
Map file
  -> GridMapLoader（# / . / P / E / D -> TileType）
  -> GridMap（tile + player/monster/exit marker）
  -> World
      +-> PlayerController -> GridCollision       (Gameplay collision)
      +-> MapGeometryGenerator -> floor/wall/door MapGeometry
                              -> MapRenderer       (Presentation)
```

碰撞直接查詢 `GridMap::IsSolid`；它不碰撞 `Object3d`、OBJ 或 `MapGeometry`。白門是
可穿越的 presentation marker，玩家中心進入 `NextMapExit` tile 才觸發換圖。牆面
Rendering 則只在 walkable/solid 邊界產生 quad surface，不為每個 solid cell
建立 Gameplay cube。

## Ownership 與生命週期

- `main` 在 stack 建立一個 `Application` 與一個 `Game`，然後呼叫
  `Application::Run(Game&)`。
- `Application` 以區域 RAII guard 擁有 KamataEngine lifetime；另一個 guard
  保證先 `Game::Finalize()`，再 `KamataEngine::Finalize()`。
- `Game` 以 `std::unique_ptr<Impl>` 隱藏組裝細節。`Impl` 保存已驗證的 map definitions、
  `GameFlow`、input/UI/camera/controller，以及唯一 active `std::unique_ptr<LevelSession>`。
- `LevelSession` 擁有 active `World`、`Player` 與 `MapRenderer`。換圖先完整建立 candidate，
  成功後才替換，未來 enemy/projectile 也應歸入這個 simulation ownership boundary。
- `Game::Initialize` 先建立暫時的 `Impl`；全部 subsystem 初始化成功後才提交
  到 `impl_`。中途失敗由區域物件解構清理，不留下半初始化的 Game。
- `World` 以 `std::optional` 擁有 map 與 settings，不保存外部裸 pointer。
- `FirstPersonCamera`、`MapRenderer` 與 `GameUiRenderer` 使用 PIMPL，公開 header 不需要完整 engine
  型別。`InputSystem` 以 `unique_ptr` 隱藏只存在於 `src/` 的 Win32
  `MouseCapture`。
- `MapRenderer::Impl` 以 `unique_ptr` 擁有 Model/Object3d；Object3d 只持有
  non-owning Model pointer，成員宣告順序保證 objects 先於 models 銷毀。
- 沒有以 `shared_ptr` 代替不清楚的 ownership，也沒有裸 owning `new/delete`。

## 初始化流程

```text
WinMain
  -> Application::Run(Game)
  -> 將 working directory 設為 executable directory
  -> KamataEngine::Initialize
  -> Game::Initialize(GameConfig)
       1. 依 mapPaths 順序載入並驗證所有 GridMap
       2. Configure PlayerController，preflight 每張圖的 spawn/collision/CPU geometry
       3. Initialize Camera/UI/Input
       4. 建立第一個 LevelSession，提早驗證 3D assets
       5. GameFlow 設為 MainMenu，cursor capture 關閉
  -> 進入 frame loop
```

設定驗證留在擁有該語意的 subsystem：World 驗證 cell size/wall height，
PlayerController 驗證 movement/look settings，FirstPersonCamera 驗證 FOV 與
clip planes，MapRenderer 驗證 resource/model 名稱與檔案。

## 每幀流程

```text
KamataEngine::Update / message pump
  -> FrameTimer::Tick（delta 最大 0.05 秒）
  -> Game::Update
       1. InputSystem::Sample -> raw InputState
       2. GameFlow 處理 keyboard/mouse menu、Escape 與 focus-loss pause
       3. screen transition frame 只切換 capture/level，不推進 simulation
       4. stable Playing 才呼叫 LevelSession::Update
       5. PlayerController + GridCollision 更新 player，Game 同步 camera
       6. 玩家中心進入 D tile 時建立下一關；最後一關則 reset 回 MainMenu
  -> DirectXCommon::PreDraw
  -> Game::Draw -> optional MapRenderer -> optional GameUiRenderer overlay
  -> DirectXCommon::PostDraw
```

`InputState` 是當幀實體狀態，不帶 `moveForward`、`menuConfirm` 等命令。`GameFlow` 是
engine-independent contract；只有穩定的 Playing frame 回報 `simulateGameplay=true`。
視窗未 capture 時不套用 mouse look；失焦會進入 Paused，重新聚焦不會自動 resume。

## Extension points

目前只預留乾淨方向，不提前建立 placeholder framework：

- **Weapon**：新增 `Gameplay/Weapon` 狀態與 controller，由 Game 將 raw input
  轉交；Weapon 不應直接呼叫 KamataEngine Input。
- **Raycast**：在 Collision 提供可重用 query/result（distance、hit point、normal
  與 target），不要把 API 寫死成 Player movement，也不要 raycast render mesh。
- **Enemy**：從 `GridMap::GetMonsterSpawnCells` 建立 Gameplay XZ state；顯示可新增獨立 billboard/sprite renderer，
  不必塞進只處理 static map surface 的 `MapRenderer`。
- **Damage**：留在 Gameplay domain，由 Weapon hit result 作用於 Player/Enemy
  state，不依賴 Rendering。
- **Enemy AI**：只依賴 World/Collision queries 與自己的狀態，不讀 Input 或
  native Camera。
- **設定**：新增 subsystem 設定時擴充 `GameConfig` 的對應 section，驗證仍由
  subsystem 負責。

## Non-goals

目前不導入：

- ECS、scene graph framework、dependency injection framework
- Full 3D physics、terrain、樓梯、跳躍、高低差或多樓層
- Reflection、serialization framework、script VM、plugin system
- Generic asset pipeline、job system 或 multiplayer architecture
- Weapon、Enemy、Raycast Combat、Damage、Enemy AI 的預先實作

Gameplay ground 目前仍是固定 `Y = 0`；畫出的 floor mesh 不是地面碰撞資料。

`NoviceResources/` 也不是待清理的舊目錄。它是 KamataEngine 的既有資源來源，
build 後部署為 `Resources/`；在沒有同步修改 engine resource lookup 前，不改名
或另建平行 asset pipeline。

## Remaining risks

- Visual Studio 2026 x64 的 Debug／Release build 與 CTest 已通過；但 headless 測試不能證明 executable 啟動與 GPU 畫面、
  shader/OBJ/MTL 最終呈現、focus/cursor capture 與實際操作手感。Menu hover/click、
  pause、白門、map transition、WASD 與 mouse look 仍需人工 runtime regression。
- `clion-debug`／`clion-release` preset 的 configure 與 Ninja build graph 已驗證；
  目前 Codex Windows runner 在 Ninja 等待 MSVC child process 時停滯，完整
  CLion/Ninja link 尚未在此 runner 證實。Visual Studio generator 不受影響。
- Rendering adapter 仍受 KamataEngine ABI、Windows 與 resource-root 規則約束；
  這些依賴已隔離，但尚未具備跨平台 backend。
- `MapRenderer` 目前為每個 surface 建立一個 Object3d；更大的地圖可能需要
  batching/instancing，但 MVP 規模尚未證明需要提早最佳化。
- `GridCollision::MoveCircle` 採固定 X 後 Z 的 axis resolution；可提供滑牆與
  防穿透，但未來高速物件、不同 shape 或精確 ray hit 需要新 query，而不是
  持續擴張 Player-specific movement function。
- Static map geometry 會在進入 level 時重建；同一 level 內的 runtime tile mutation/reload 尚未設計。

本文件描述程式碼責任與已知驗證邊界；不把通過的 headless contract tests
延伸解讀為完整人工 gameplay／visual regression。
