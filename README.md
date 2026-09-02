# Object_Connect

`Object_Connect` 是以 C++20／KamataEngine 製作的固定 1280×720 2D
血管連線解謎。玩家從已激活節點拉出程序化血管，在有限的全域長度內選擇
路徑，避開 solid tile，並激活關卡資料指定的所有 goal。

專案以 Game Jam 與新人可讀性為優先：沒有 ECS、scene graph、完整物理引擎
或通用腳本層。關卡採嚴格 JSON，core 不依賴 KamataEngine；血管以 8–12 個
Verlet 粒子、距離約束及像素化 triangle strip 即時生成。

## 遊戲規則

- 一關可以有多個 root、goal，也可以重複使用同一種器官圖。
- 所有 root 開局激活。任何已激活且仍有 outgoing 容量的節點都能繼續拉線。
- 每條連線必須是關卡 JSON 明列的有向 edge，而且只能提交一次。
- 提交會各消耗 source 的一個 outgoing 與 target 的一個 incoming；target 立即
  激活。source 不會失效，因此資料可以形成分支；target 也能再接收其他 edge，
  因此可以匯合。
- 所有 goal 都激活時通關。程式不解讀 `HEART`、`BRAIN` 等名稱，角色完全由
  `is_root`／`is_goal` 決定。
- 所有已固定段落與目前 preview 共用唯一 `total_length`。
- solid obstacle tile 會阻擋 anchor-to-anchor 的直線。血管粒子不做障礙碰撞；
  要繞路必須在資料中放置中繼節點。

核心永遠維持：

```text
committed segments + preview reservation + remaining = total_length
```

拖曳距離乘上 `minimum_slack_ratio` 後成為 reservation；游標命中授權 target 的
任一 occupied cell 時，還會至少保留 anchor-to-anchor 的最短需求。拖遠只會增加，
拖回不會自動縮短；取消會完整退款，成功則永久扣除。若前段使用過多，按 `R`
重開整關。

若尚有合法 edge 但剩餘長度都不足，HUD 顯示 `NOT ENOUGH LENGTH`；若是路線或
容量選擇造成完全沒有可用 edge，則顯示 `NO VALID CONNECTIONS`。兩者都可用
`R` 重新規劃。

`PuzzleBoard::GetActivatedNodeIndices()` 與
`GetCommittedConnectionIndices()` 是後續計分系統的唯讀接口；本 MVP 不把
器官分數規則寫入 board。

## 畫面流程與操作

```text
MAIN MENU -> LEVEL SELECT -> PLAYING
                              | Esc / focus lost
                              v
                            PAUSED
                              |
                              +-> RESUME / LEVEL SELECT / MAIN MENU / EXIT GAME

PLAYING -- all goals active --> SOLVED
                                 NEXT PUZZLE / LEVEL SELECT / RETRY
```

- `W`／`↑`、`S`／`↓`：選單移動。
- `Enter` 或左鍵：確認。
- 在可拉線節點的 occupied tile 上按住左鍵，拖到合法 target 後放開。
- `Esc`：暫停／返回。
- `R`：重玩關卡。

選關畫面每頁最多顯示七項，鍵盤移動到下一頁的項目時會自動換頁；滑鼠命中會映射
回完整 catalog 的關卡 index。

失焦會立即取消 preview 並暫停。Paused 不推進任何血管模擬。

## JSON 關卡資料

執行入口是 `Resources/data/catalog.json`；原始檔位於
`NoviceResources/data/`：

```text
catalog.json                 canvas、共用檔案與關卡列表
tileset.json                 atlas path 與 tile ID -> atlas cell
node_types.json              node type、顯示名稱、tile stamp、anchor cell
levels/*.json                兩個 80x45 layer、node instances、edges、規則
tiles/organ_atlas.png        單一 16px-cell placeholder atlas
```

固定規格是 1280×720、16px tile、80×45 格。tile ID `0` 一律代表透明／空白；
`obstacles` layer 的所有非零格都是 solid。Node instance 的 `column`／`row`
是 stamp 左上角；node type stamp 的非零 cell 同時是圖像與點擊 mask，anchor
必須落在 occupied cell。

每個 node instance 提供：

```text
id, type_id, column, row,
is_root, is_goal, max_incoming, max_outgoing
```

每個 connection 提供穩定 ID、`from`／`to`，以及 `point_count`、粗細、跟隨
延遲與初始方向。候選圖必須是 DAG；每個節點都必須可從至少一個 root 到達。
候選 edge 可以多於容量，讓玩家選路。Loader 不自動證明容量與總長度能同時
完成所有 goal，這是關卡設計者的實玩責任。

Loader 會拒絕未知欄位、未知 `schema_version`、不安全路徑、未知 tile／type／
node、錯誤矩陣尺寸、stamp/anchor 問題、node 重疊、node 與 solid 重疊、錯誤
容量、cycle、不可達節點及被 solid tile 阻擋的 edge。載入是 transactional；
失敗不會留下 partial catalog，診斷包含檔名與 JSON pointer。

磁碟載入要求 catalog、tileset、node types、levels 與 atlas 都是 canonical
resource root 內的 regular file；symlink／junction 也不能指向 root 外。

路徑相對於「寫下該路徑的 JSON 檔」解析；例如 `atlas_path` 相對於
`tileset.json`，載入後才正規化為 resource-root-relative path。Loader 會確認
atlas 是 resource root 內的既有 `.png`，但不在 core 內解碼圖片尺寸。ID 必須由
小寫字母開頭、使用 lower snake case，且不能有連續或結尾底線。文字必須含至少
一個非空白的 printable ASCII 字元。`width_variation` 範圍是 `[0, 1)`，且
`base_width >= tip_width`。

正式 JSON Schema 與穩定 ID 規則位於 `Docs/Schemas/`。Runtime 使用 vendored
`nlohmann/json` 3.12.0；JSON DOM 只存在 loader 實作內。

## 內建關卡

- `FIRST LINK`：單 root 到單 goal，顯示提示。
- `AROUND THE RIB`：中央 bone tiles，上下兩條器官路徑可選。
- `DOUBLE CIRCULATION`：兩個 root、重複肺部、分支、匯合與兩個 goal。

`tools/GenerateBuiltInTileData.ps1` 可重建這三關的明確 45×80 JSON 矩陣與
placeholder atlas。它只是可重現的 sample-content 產生器，不是關卡編輯器。

## 建置、執行與測試

需求：Windows x64、Visual Studio 2026/MSVC v145、Windows SDK、CMake，以及
KamataEngine（預設 `D:\code\Runtime\KamataEngine`）。

```powershell
.\Build.ps1 -Configuration Debug
.\Build.ps1 -Configuration Release
.\Run.ps1 -Configuration Debug

ctest --test-dir build/vs2026-x64 -C Debug --output-on-failure
ctest --test-dir build/vs2026-x64 -C Release --output-on-failure
```

建置會把 `NoviceResources/` 同步到 `target/<Configuration>/Resources/`。
`Object_Connect_CoreTests` 是 headless，不建立視窗或 GPU。

## 程式結構

```text
include/ObjectConnect/         公開 API
src/main.cpp                   Win32 entry point
src/ObjectConnect/
  Core/                        app lifetime、frame timer
  Data/                        strict JSON loader、normalized definitions
  Game/                        screen flow 與高層組裝
  Geometry/                    tile/segment 純幾何
  Input/                       KamataEngine input adapter
  Puzzle/                      PuzzleBoard runtime state
  Rendering/                   tile batch、pixel ribbon、UI
  Tentacle/                    Verlet 與 ribbon builder
tests/                         headless core contract tests
Docs/Schemas/                  schema version 1 文件
third_party/nlohmann/          pinned JSON header 與 MIT license
```

建議新人依序閱讀 `PuzzleData.hpp`、一份 `levels/*.json`、
`PuzzleCatalogLoader.cpp`、`PuzzleBoard.cpp`、`BloodTentacle.cpp`、renderer，最後
才看 `Game.cpp` 的組裝。

## 非目標

本次不製作關卡編輯器、TMJ/TXT importer、自動解題器、存檔、解鎖進度、熱重載、
音訊、動畫 effect system、血管粒子對 tile 的物理碰撞、逐段撤銷、可移動 body、
creature controller、ECS 或可調解析度。
