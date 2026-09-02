# Object_Connect

`Object_Connect` 是以 C++20 與 KamataEngine 製作的 2D 節點連線解謎 MVP。玩家從目前亮起的末端拖出一條血管，在合法候選節點中選擇下一站；所有已完成段落和正在拖曳的段落，共用關卡唯一的一筆 `total_length`。

這個專案以 Game Jam 的開發速度與新人可讀性為優先：核心規則不依賴 KamataEngine、資料使用四份小型 CSV、流程沒有 fade、沒有 ECS 或完整物理引擎。血管不是 sprite 動畫，而是由 8–12 個 2D 粒子、Verlet integration、距離約束和 triangle strip 即時生成，再以格點量化、深色外緣與程序化肉質像素呈現。

## 遊戲流程

```text
MAIN MENU
  PLAY -> LEVEL SELECT -> PLAYING
  EXIT                    |
                          +-- Esc 或失焦 -> PAUSED
                          |                  RESUME
                          |                  LEVEL SELECT
                          |                  MAIN MENU
                          |                  EXIT GAME
                          |
                          +-- 連到路徑終點 -> SOLVED
                                             NEXT PUZZLE（非最後一關）
                                             LEVEL SELECT
                                             RETRY
```

所有關卡一開始就能選擇，不保存解鎖進度。切換、離開或重玩關卡時會建立新的 `PuzzleBoard`，因此整關長度與連線狀態會完整重置。末端抵達唯一終點後約 0.6 秒才顯示完成選單。

## 操作

- `W`／`↑`、`S`／`↓`：選單上一項／下一項。
- `Enter` 或滑鼠左鍵：確認選單項目。
- 在目前亮起的末端節點按住滑鼠左鍵，拖到任一合法候選 target，放開後完成連線。
- `Esc`：遊戲中暫停；暫停中回到遊戲；選關畫面回主選單；完成畫面回選關。
- `R`：遊戲中或暫停中重玩目前關卡。

滑鼠游標全程可見，不會被 capture。視窗失去焦點時，正在拖曳的 preview 會立即取消，遊戲自動暫停；重新取得焦點不會自動恢復。

## 全域長度規則

每關只有一個 `total_length`。每個節點不擁有自己的血管長度，各個連線段落都從同一筆預算扣除：

```text
已固定段落長度總和
+ 本次拖曳暫時保留的長度
+ 可用剩餘長度
= 關卡 total_length
```

拖曳距離會乘上 `minimum_slack_ratio`，得到這次需要保留的長度：

```text
reservedLength = max(
    previousReservedLength,
    distance(source, cursor) * minimumSlackRatio
)
```

因此拖遠只會增加保留量，拖回來不會自動縮短。玩家可以先拉遠，再回到 target，刻意留出更多鬆弛量；但前段用太多，後段就可能沒有足夠長度，只能按 `R` 重開整關。游標超出剩餘長度能到達的範圍時，tip 會被限制，血管呈現繃緊效果。

放開時必須落在目前末端的一個候選 target、長度足夠，而且 source 到 target 的直線沒有被障礙物阻擋。成功時保留量成為固定段落的 rest length，選中的 target 成為唯一新末端；舊節點不能再拉出分支。失敗、放在空白或放錯節點時，preview 約 0.22 秒收回，保留量完整退還。

畫面左上會顯示 `REMAINING n / total`。剩餘量無法完成下一段時會以紅色顯示 `NOT ENOUGH LENGTH`。

核心不內建器官分數。`PuzzleBoard::GetVisitedNodeIndices()` 與
`GetCommittedConnectionIndices()` 會以唯讀方式提供玩家實際選中的路徑，後續系統可據此計算經過器官數、加權分數或彩蛋獎勵，而不把背景名稱寫死在解謎規則裡。

## 內建關卡

- `FIRST LINK`：A → B，無障礙，顯示答案提示線。
- `AROUND BLOCK`：A → B → C → D，中央有矩形障礙，顯示答案提示線。
- `CLOT PATH`：七個節點與多條候選路徑，中央有矩形與圓形障礙，不顯示答案提示線；可選直達、短路線或較長路線。

三關都保留有限容錯，且不同連線可設定粒子數、粗細、跟隨延遲與初始方向。

## 必要環境

- Windows x64。
- Visual Studio 2026，安裝「使用 C++ 的桌面開發」、MSVC v145 與 Windows SDK。
- 支援 `Visual Studio 18 2026` generator 的 CMake；`Build.ps1` 會優先使用 Visual Studio 內附版本。
- KamataEngine。預設位置是 `D:\code\Runtime\KamataEngine`。

專案使用 C++20，MSVC 以 `/W4 /WX /sdl /permissive- /utf-8` 編譯。Debug 與 Release 都必須保持 warning-free。

## 建置與執行

Debug：

```powershell
.\Build.ps1 -Configuration Debug
```

Release：

```powershell
.\Build.ps1 -Configuration Release
```

KamataEngine 位於其他位置時：

```powershell
.\Build.ps1 -Configuration Debug -KamataEngineRoot "D:\your\KamataEngine"
```

建置後執行：

```powershell
.\Run.ps1 -Configuration Debug
```

略過重建、直接執行既有 binary：

```powershell
.\Run.ps1 -Configuration Debug -SkipBuild
```

執行檔位於 `target/<Configuration>/Object_Connect.exe`。CMake 每次建置都會把 `NoviceResources/` 同步到執行檔旁的 `Resources/`；程式啟動時也會把 working directory 設為執行檔目錄，所以資料和 shader 路徑不依賴啟動位置。

若在 CLion 使用 x64 Visual Studio toolchain，可選 `clion-debug` 或 `clion-release` CMake preset。

## 執行測試

先建置對應 configuration，再執行：

```powershell
ctest --test-dir build/vs2026-x64 -C Debug --output-on-failure
ctest --test-dir build/vs2026-x64 -C Release --output-on-failure
```

`Object_Connect_CoreTests` 不建立視窗，也不需要 GPU。測試涵蓋 CSV 與正式四表整合、候選 DAG、路徑選擇、全域長度不變量、障礙幾何、末端解鎖、血管約束與下垂、跟隨延遲、ribbon 頂點與像素格點，以及主選單／選關／暫停／完成流程。實際拖曳手感、肉質像素密度、triangle strip 裂縫、圖層順序和失焦行為仍需執行遊戲人工確認。

## CSV 關卡資料

原始資料位於 `NoviceResources/data/`，執行時從 `Resources/data/` 讀取。不是 JSON，也沒有內建關卡程式碼；四份 CSV 合在一起描述整個 puzzle catalog。

### `levels.csv`

```text
puzzle_id,title,start_node_id,total_length,minimum_slack_ratio,background_color,show_target_connections,vessel_color,base_width,tip_width,width_variation
```

- 每個 `puzzle_id` 一列。
- `total_length` 使用 1280×720 的邏輯像素，範圍為 `(0, 100000]`。
- `minimum_slack_ratio` 範圍為 `[1, 4]`，建議從 1.05 開始。
- `show_target_connections=true` 會顯示低透明度答案線。
- `base_width`、`tip_width`、`width_variation` 控制整關的血管外觀。兩個寬度相等會取消根部到末端的漸縮，variation 只向內切出局部凹缺；內建關卡使用 16 像素外輪廓、`0.16` 凹凸量與深紅色 `#861B2B`。Renderer 再套用 2 像素格點、深色外緣及亮紅／暗紅肉質斑塊。

### `nodes.csv`

```text
puzzle_id,node_id,label,x,y,radius,color
```

節點圓形必須完整位於 1280×720 畫布內，不得互相重疊，也不得與障礙重疊。

### `connections.csv`

```text
puzzle_id,from_node_id,to_node_id,point_count,thickness_scale,follow_delay_seconds,initial_direction_degrees
```

- 連線有方向，從 `start_node_id` 形成候選路徑圖；資料可有多條 outgoing／incoming，但玩家實際血管始終只選一條。
- 所有 connection 必須從起點可達，整張圖必須無循環且只有一個可達終點。
- 不允許循環、自連線、重複 edge、未知 node、斷開的 connection 或多終點。
- `point_count` 必須是 8–12。
- `thickness_scale` 最大為 8；同一路徑所有段落都設為 `1` 時，跨節點也會維持一致粗細。`follow_delay_seconds` 最大為 1 秒，初始方向限制為 `[-360, 360]` 度。
- Loader 保留 CSV 列順序；遊戲依目前末端和玩家選中的 target 決定路徑，不以列順序決定流程。

### `obstacles.csv`

```text
puzzle_id,obstacle_id,shape,center_x,center_y,width,height,radius,color
```

- `shape` 只接受 `rectangle` 或 `circle`。
- 矩形填 `width`／`height`，`radius` 留空。
- 圓形填 `radius`，`width`／`height` 留空。
- 障礙必須完整位於畫布內。

顏色接受 `#RRGGBB` 或 `#RRGGBBAA`。ID、標題與 label 使用可顯示的英文 ASCII。CSV reader 接受 UTF-8 BOM、LF／CRLF、quoted field、quoted newline 與 `""` quote escape；header 名稱與順序則必須完全符合契約。

啟動時 loader 會一次驗證四表：型別、finite 數值、安全範圍、重複 ID、跨表引用、拓樸、畫布範圍、重疊、障礙阻擋，以及：

```text
shortest_path_sum(distance(from, to)) * minimum_slack_ratio <= total_length
```

未選候選邊不計入長度；Loader 只要求至少一條起點到終點的路線可以完成。血管 clearance 會用該段最大半寬再加一個 2 像素格點擴張障礙，涵蓋像素量化位移；碰到邊界也算阻擋。任一錯誤都會使整份 catalog 載入失敗，不會留下 partial catalog；錯誤訊息包含檔名、row 與 column／field。

## 程式結構

```text
include/ObjectConnect/         公開 API；object_connect namespace
  Core/                        Application、FrameTimer
  Data/                        CSV、PuzzleData、catalog loader
  Game/                        Game、GameConfig、GameFlow
  Geometry/                    segment-circle、segment-AABB 等純 2D query
  Input/                       鍵盤、滑鼠、focus 的原始狀態
  Math/                        Vec2、Color
  Puzzle/                      PuzzleBoard 與 render snapshot
  Rendering/                   DirectX/KamataEngine renderer adapter
  Tentacle/                    Verlet simulation 與 ribbon builder
src/
  main.cpp                     Win32 程式進入點
  ObjectConnect/               實作；目錄與 include/ObjectConnect 一致
    Core/                      Application、FrameTimer
    Data/                      CSV 與 catalog loader
    Game/                      Game、GameFlow
    Geometry/                  純 2D geometry query
    Input/                     KamataEngine input adapter
    Puzzle/                    PuzzleBoard
    Rendering/                 DirectX/KamataEngine renderer
    Tentacle/                  Verlet 與 ribbon builder
tests/                         無引擎依賴的 core contract tests
NoviceResources/data/          四份關卡 CSV 原本
NoviceResources/shaders/       flat-color 2D shader
Docs/Architecture.md           依賴、ownership、每幀流程與擴充界線
```

`NoviceResources/axis/` 與 `Obj*.hlsl` 看起來像 3D 遺留物，但 KamataEngine 啟動時會固定初始化 Model／AxisIndicator；它們是引擎 bootstrap 資源，不能刪除。遊戲本身不使用 3D 模型玩法。

CMake target 的責任也保持簡單：

- `object_connect_core`：Math、Data、Geometry、PuzzleBoard、GameFlow、BloodTentacle、RibbonStrip；不依賴 KamataEngine。
- `object_connect_runtime`：Application、Input、Game、UI 與 DirectX renderer。
- `Object_Connect`：Win32 執行檔。
- `Object_Connect_CoreTests`：headless core tests。

## 新人建議閱讀順序

1. 先看 `NoviceResources/data/` 的三關，理解「四張表共同描述一關」。
2. 看 `PuzzleData.hpp`，認識 loader 產出的只讀 definition。
3. 看 `PuzzleBoard.hpp/.cpp`，追蹤拖曳、保留、退還與 commit。
4. 看 `BloodTentacle.hpp/.cpp`，理解單一段落如何模擬；不要把它和全域長度預算混在一起。
5. 看 `RibbonStrip.hpp/.cpp`，理解中心點如何變成可畫的左右頂點。
6. 最後看 `Game.cpp`，了解輸入、流程、board snapshot 與 renderer 如何組裝。

加入新關卡通常只需要編輯四份 CSV；若只改規則，優先修改 `object_connect_core` 並先補 core test。Renderer 只讀 snapshot，不應回寫 `PuzzleBoard`。

## 範圍外

本 MVP 不支援 JSON、存檔、關卡解鎖、熱重載、中文 UI、音訊、血管貼圖、粒子對障礙的物理碰撞、可移動 body、creature controller、ECS、scene graph 或完整物理引擎。固定解析度為 1280×720。
