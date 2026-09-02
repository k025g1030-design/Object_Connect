# Object_Connect

`Object_Connect` 是以 C++20 與 KamataEngine 製作的固定 1280×720、2D 血管連線解謎 MVP。

玩家從已啟動的 `root` 或 `follow` 節點拉出血管，自由連到可接受輸入的 `follow`／`end` 節點。連線不是事先寫在資料裡：每次拖曳時，`PuzzleBoard` 依節點容量、長度、重複邊、循環和 `dead` 節點的直線阻擋即時判斷是否合法。

專案以 Game Jam 速度和新人可讀性為優先。核心玩法不依賴 KamataEngine，沒有 ECS、scene graph 或完整物理引擎。血管以 Verlet 粒子鏈與 triangle strip 即時生成，不是 sprite-sheet 動畫。

## 目前的遊戲流程

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
                          +-- 所有已放置 end 啟動 -> SOLVED
                                                   NEXT PUZZLE（有效 nextLevelId）
                                                   LEVEL SELECT
                                                   RETRY
```

選關順序就是 `levels.csv` 的列順序。`NEXT PUZZLE` 則不依賴列順序：它讀取目前關卡的 `next_level_id`，並以 ID 在 catalog 中查找。欄位為空或找不到該 ID 時，不顯示 Next。

切換、離開或重玩關卡都會重建 `PuzzleBoard`，所以節點狀態、連線和長度完整重置。通關後約 0.6 秒才開放完成選單。

## 操作

- `W`／`↑`、`S`／`↓`：選單上一項／下一項。
- `Enter` 或滑鼠左鍵：確認選單項目。
- 在有脈動提示的可用 source 上按住滑鼠左鍵，拖到合法 target 後放開。
- `Esc`：遊戲中暫停；暫停中回到遊戲；選關畫面回主選單；完成畫面回選關。
- `R`：遊戲中或暫停中重玩目前關卡。

游標全程可見且不會被 capture。視窗失焦時，正在拖曳的 preview 會立即取消並自動暫停；重新取得焦點不會自動恢復。

## 節點與動態連線

每個 map 可以放置四種節點：

| `node_type` | Runtime 行為 |
| --- | --- |
| `root` | 關卡開始時立即 active；可作為 source。可以有多個 root。 |
| `follow` | 可作為 target；第一次接入後變成 active，也可繼續作為 source。 |
| `end` | 可作為 target，不能作為 source。所有已放置的 end 都 active 時通關。 |
| `dead` | 不能連接；目前作為矩形直線視線障礙。 |

連線不由資料預先列出。只要符合以下條件，active 的 root／follow 都可以開始新連線：

- source 的 `max_outgoing` 尚未用完。
- source 的 `max_outgoing_length` 還有剩餘量。
- 關卡全域 `total_length` 還有剩餘量。
- target 是有 placement 的 follow／end，且 `max_incoming` 尚未用完。
- 不是 self connection、不是重複的有向 edge，也不會在已提交圖中形成 cycle。
- source 中心到 target 中心的直線沒有穿過擴張後的 dead AABB。

因此 runtime graph 可以依容量形成分支或合流；已啟動且還有資源的舊 source 仍可再次使用。

map 中沒有 `tile_x`／`tile_y` 的節點只保留資料，不會顯示、命中、阻擋或參與通關判定。

## 兩層長度預算

所有連線共享一筆關卡總長度：

```text
所有已提交連線長度
+ 目前 preview 保留長度
+ 全域剩餘長度
= total_length
```

每個可當 source 的節點另有自己的 outgoing 長度上限：

```text
該 source 已提交 outgoing 長度
+ 若 preview 從它拉出，preview 保留長度
+ 該 source 的 outgoing 剩餘長度
= max_outgoing_length
```

一次 preview 真正能部署的上限是：

```text
min(全域剩餘長度, source outgoing 剩餘長度)
```

拖曳需要的長度為 `distance(source, cursor) * minimum_slack_ratio`。保留量只會增加，游標拖回來不會自動縮短，所以玩家可以主動留下鬆弛，也可能提早花掉過多預算。放空白、放到非法 target 或被 dead 阻擋時，preview 約 0.22 秒收回，保留量逐步退還。

HUD 顯示 `REMAINING n / total`。當穩定狀態下仍有結構上可連的 target，卻沒有足夠的全域／source 長度完成任何一條時，顯示 `NOT ENOUGH LENGTH`。

## Dead 障礙目前做到哪裡

`dead` 節點以 `tile_x`／`tile_y`、`width_tiles`、`height_tiles` 形成矩形 AABB。Board 會用血管最大半寬擴張 AABB；拖曳時，source-center 到游標的射線若先碰到 dead，preview tip 會停在最早接觸點之前。提交時仍會再次測試 source-center 到 target-center 的線段；碰到邊界也算阻擋。

目前做到 tip clamp 與 commit line-of-sight：

- Preview tip 不能直接拖穿 dead，但血管中間的 Verlet 粒子和已固定段落不會對 dead 做 collision response。
- 血管不會自動尋路或沿障礙彎曲。
- Renderer 在血管之後繪製 dead 貼圖或矩形 fallback，遮住部分視覺穿越。

如果未來要做真正繞骨、粒子碰撞或路徑規劃，需要另外設計 solver；這些尚未實作。

## 三層 CSV 資料

原始資料位於 `NoviceResources/data/`，建置後同步到 `Resources/data/`：

```text
data/
  levels.csv             關卡順序、map 路徑、next ID、全域預算與外觀
  nodes.csv              可重用的節點 preset catalog
  maps/
    first_link.csv       各關卡實際節點 instance 與 placement
    around_block.csv
    clot_path.csv
```

### `levels.csv`

```text
level_id,level_name,map_path,next_level_id,total_length,minimum_slack_ratio,background_color,vessel_color,base_width,tip_width,width_variation
```

- CSV 列順序決定 Level Select 順序。
- `map_path` 是相對 `Resources/` 的安全路徑。
- `next_level_id` 可留空；runtime 只有在 ID 確實存在時才提供 Next。
- `total_length` 是全域長度預算。
- `minimum_slack_ratio` 空白時預設 1.05。
- 顏色接受 `#RRGGBB` 或 `#RRGGBBAA`。
- 空白的背景、血管與寬度欄位會使用 loader 預設值。

### `nodes.csv` preset catalog

```text
preset_id,node_type,texture_path,width_tiles,height_tiles,display_name,max_incoming,max_outgoing,max_outgoing_length
```

`NodePresetCatalogLoader` 可獨立載入這份檔案；遊戲啟動使用的 `PuzzleCatalogLoader` 也會載入它，作為 map instance 的預設值來源。

Map row 有 `source_preset_id` 時，Loader 先複製該 preset，再用 map 中「非空白」的欄位覆寫。這讓共用器官只需在 `nodes.csv` 維護一次，而每關仍可調整尺寸、名稱、貼圖或容量。

### 每關 map CSV

```text
instance_id,source_preset_id,node_type,texture_path,width_tiles,height_tiles,display_name,tile_x,tile_y,max_incoming,max_outgoing,max_outgoing_length
```

- `instance_id` 在該 map 內唯一。
- `source_preset_id` 可留空；非空時必須對應 `nodes.csv` 的既有 preset。
- `node_type` 只接受 `root`、`follow`、`end`、`dead`。有 preset 時可留空並繼承；沒有 preset 時必填。
- 一格固定為 16×16 邏輯像素；`tile_x`／`tile_y` 是矩形左上角。
- `width_tiles`／`height_tiles` 決定節點 AABB。
- `tile_x` 和 `tile_y` 必須一起填或一起留空。
- `width_tiles`、`height_tiles`、`display_name`、`texture_path` 與三個容量欄位都遵循相同規則：有 preset 時空白代表繼承，非空白代表覆寫；沒有 preset 時空白保留型別預設值。
- 空白無法明確清除 preset 已提供的 `display_name` 或 `texture_path`。需要無名稱／無貼圖的 instance 時，請使用對應欄位本來就是空白的 preset，或不指定 preset。
- resolved `display_name` 空白時 UI 不顯示名稱；resolved `texture_path` 非空時 loader 會確認資源存在，renderer 會顯示該貼圖。
- root／follow 要能作 source，就必須有非零 `max_outgoing` 和 `max_outgoing_length`；follow／end 要能被接入，就必須有非零 `max_incoming`。

CSV header 名稱與順序必須完全一致。Reader 支援 UTF-8 BOM、LF／CRLF、quoted field、quoted newline 與 `""` quote escape。ID 使用 lower_snake_case；顯示文字不得包含控制字元。載入採暫存後提交，失敗不會留下 partial catalog，診斷包含檔名、列與欄位。

目前 Loader 著重欄位、路徑和基本型別驗證；它尚未做完整的畫布範圍、節點互相重疊或關卡可完成性 preflight。這些限制不要在資料中故意依賴。

## 目前顯示方式

`PuzzleRenderer` 以 flat-color DirectX 12 pipeline 畫背景、血管、提示與無貼圖 fallback，並以 KamataEngine sprite 顯示有 `texture_path` 的節點：

```text
背景
-> 血管深色外緣 / 深紅 core / 程序化肉質像素
-> dead 貼圖或矩形 fallback
-> source pulse
-> root / follow / end 貼圖或矩形 fallback
-> ASCII HUD / 選單 overlay
```

Dormant 節點會暗化，active 節點使用正常顏色，可拉出的 source 顯示脈動提示。無 placement 的節點不繪製；`display_name` 空白時不畫文字。

進入關卡時，renderer 依 resolved `texture_path` 載入並建立節點 sprite，再按 `width_tiles × height_tiles` 和 placement 繪製。同一關內相同路徑共用 texture handle；切關時沿用兩關共有的 handle，釋放只屬於舊關的 handle，因此每幀 Draw 不會重複載入，也不會讓歷史關卡貼圖一直佔用 descriptor。UI 與關卡 renderer 透過同一個引用計數 registry 共用 handle，避免其中一方提早卸載。每關最多 255 個有 placement 的 unique node texture path；這讓舊、新兩關能在 transactional 切換期間同時存在於 512-path registry，失敗時可完整保留原關卡。空路徑才使用依 node type 著色的矩形 fallback。Dead 貼圖和 fallback 都畫在血管之上。

## 內建資料

- `FIRST LINK`：Heart 與 Brain，測試最小 root → end 連線。
- `AROUND BLOCK`：Heart、Lung、Liver、Brain，加上一個矩形 dead 區域。
- `CLOT PATH`：多個 follow 器官、Brain 和兩個 dead 區域，可測試多 source、容量與長度取捨。

這些 map 沒有預先連線；實際路線全部由玩家在 runtime 決定。

## 建置與執行

需要 Windows x64、Visual Studio 2026 C++ Desktop workload、支援 `Visual Studio 18 2026` generator 的 CMake，以及 KamataEngine。預設 engine 路徑為 `D:\code\Runtime\KamataEngine`。

```powershell
.\Build.ps1 -Configuration Debug
.\Build.ps1 -Configuration Release
.\Run.ps1 -Configuration Debug
```

KamataEngine 位於其他位置時：

```powershell
.\Build.ps1 -Configuration Debug -KamataEngineRoot "D:\your\KamataEngine"
```

執行檔位於 `target/<Configuration>/Object_Connect.exe`。建置會把 `NoviceResources/` 同步到執行檔旁的 `Resources/`；程式啟動時會把 working directory 設為執行檔目錄。

專案使用 C++20，MSVC 設定為 `/W4 /WX /sdl /permissive- /utf-8`。

## 測試

```powershell
ctest --test-dir build/vs2026-x64 -C Debug --output-on-failure
ctest --test-dir build/vs2026-x64 -C Release --output-on-failure
```

`Object_Connect_CoreTests` 不建立視窗，也不需要 GPU。核心測試涵蓋 CSV、三層資料 contract、AABB 幾何、動態 source／target、容量與雙層長度預算、duplicate／cycle／dead LOS、preview refund、BloodTentacle、RibbonStrip 和 GameFlow。

GPU 畫面、拖曳手感、遮擋與 HUD 排版仍需要人工確認。

## 程式結構

```text
include/ObjectConnect/         公開 API；object_connect namespace
  Core/                        Application、FrameTimer
  Data/                        CSV、PuzzleData、兩個 catalog loader
  Game/                        Game、GameConfig、GameFlow
  Geometry/                    AABB 純 2D query
  Input/                       鍵盤、滑鼠與 focus 原始狀態
  Math/                        Vec2、Color
  Puzzle/                      PuzzleBoard 與 render snapshot
  Rendering/                   DirectX/KamataEngine adapters
  Tentacle/                    Verlet simulation 與 ribbon builder
src/ObjectConnect/             與 include 對稱的實作
tests/                         無引擎依賴的 core tests
NoviceResources/data/          levels、presets 與 per-level maps
NoviceResources/shaders/       flat-color 2D shaders
Docs/Architecture.md           ownership、資料流與擴充界線
```

`NoviceResources/axis/` 與 `Obj*.hlsl` 是 KamataEngine bootstrap 所需資源；本作不使用 3D gameplay，但不能直接刪除。

## 新人閱讀順序

1. 看 `NoviceResources/data/levels.csv` 和 `data/maps/`，了解關卡與 placement。
2. 看 `PuzzleData.hpp`，認識四種 node 與 16px tile helpers。
3. 看 `PuzzleCatalogLoader.cpp`，了解 preset 繼承、map override 與 runtime catalog 的組裝。
4. 看 `PuzzleBoard.hpp/.cpp`，追蹤 active nodes、dynamic commit 與兩層 budget。
5. 看 `BloodTentacle.hpp/.cpp` 和 `RibbonStrip.hpp/.cpp`。
6. 最後看 `Game.cpp`，了解 session、`nextLevelId` 與 renderer 組裝。

Renderer 只讀 snapshot，不應回寫 `PuzzleBoard`。新增關卡通常只需在 `levels.csv` 增加一列並新增一份 map CSV，不要在 `Game.cpp` 寫死關卡。

## 尚未實作

- Dead 的 Verlet 粒子碰撞、血管繞障礙或路徑搜尋。
- 完整資料幾何 preflight，以及 preset/map 的編輯器或回寫工具。
- 器官分數、出血／血壓倒數、存檔與解鎖進度。
- 音訊、熱重載、localization、ECS、完整物理或 creature controller。
