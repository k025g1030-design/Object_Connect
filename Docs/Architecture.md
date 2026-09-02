# Object_Connect 架構

## 1. 設計目標

`Object_Connect` 是固定 1280×720 的 2D Game Jam 解謎 MVP。架構只解決四件事：

1. 從四份 CSV 載入並完整驗證關卡。
2. 從候選路徑圖中管理一條不分叉、共用單一總長度的節點連線謎題。
3. 用 engine-independent Verlet 粒子鏈模擬血管。
4. 用 KamataEngine／DirectX 12 把 core snapshot 畫成 flat-color 2D 畫面。

容易閱讀比高度泛化重要。沒有 ECS、service locator、scene graph、script VM 或通用 serialization framework。公開 API 位於 `include/ObjectConnect/`，實作固定放在對稱的 `src/ObjectConnect/` 模組目錄，程式進入點是 `src/main.cpp`；不要再新增平行的 `src/<Module>/`。程式使用 `object_connect` namespace，平台與 GPU 細節留在 runtime adapter。

## 2. Target 與依賴方向

```text
Object_Connect executable
  -> object_connect_runtime
       -> Application / Input / Game / UI / PuzzleRenderer
       -> KamataEngine / DirectX 12
       -> object_connect_core
            -> Math / CSV + Data / Geometry
            -> GameFlow / PuzzleBoard
            -> BloodTentacle / RibbonStrip

Object_Connect_CoreTests
  -> object_connect_core
```

依賴只往下：core 不 include KamataEngine、Win32 或 D3D 型別；Rendering 讀取 definition 與 snapshot，不改寫 gameplay 狀態。`PuzzleRenderer` 和 `GameUiRenderer` 用 PIMPL 隔離 engine／D3D ownership，避免 GPU header 滲入公開 contract。

## 3. 模組責任

| 模組 | 擁有／負責 | 不負責 |
| --- | --- | --- |
| Core | KamataEngine lifetime、工作目錄、主迴圈、delta clamp | 解謎規則或 draw data |
| Math | `Vec2`、`Color` 和小型數學 helper | 矩陣框架或 engine adapter |
| Data | 嚴格 CSV parse、四表組裝、cross-reference 和 preflight validation | Runtime drag 狀態或渲染 |
| Geometry | point／segment 對 circle、AABB 的純函式 query | Verlet collision response |
| Tentacle/BloodTentacle | 一個段落的粒子狀態、fixed-step Verlet、tip mode、root pull output | 關卡總長度與解鎖順序 |
| Tentacle/RibbonStrip | 中心點到 `2 * pointCount` 左右頂點、可選像素格點量化 | GPU buffer 或 draw call |
| Puzzle/PuzzleBoard | 全域長度預算、目前末端、候選 connection、preview、commit、retract、完成狀態 | 選單、檔案 I/O、D3D、分數規則 |
| Game/GameFlow | MainMenu／LevelSelect／Playing／Paused／Solved 的輸入語意 | Board simulation 與畫面繪製 |
| Input | 每幀鍵盤、滑鼠位置／按下／放開、focus edge | 解釋拖曳或選單 command |
| Rendering | flat-color pipeline、圖層順序、HUD、overlay、mouse hit-test | 修改 Board 或 Flow 狀態 |
| Game | 唯一高層組裝點；catalog、board session、flow、renderer 的協調 | 低層約束或 CSV parse |

`BloodTentacle` 的「一段粒子鏈」和遊戲規則的「一條連續血管」是兩個層次。每次成功連線會保留一個 simulation instance，但所有 instance 由同一個 `PuzzleBoard` 預算與目前末端管理，所以玩法上仍是同一條血管。關卡資料可以有候選分支；玩家每次只提交其中一條，舊節點不會再次變成可操作狀態。

## 4. 資料模型與載入

原本位於 `NoviceResources/data/`，建置後同步至 `Resources/data/`：

```text
levels.csv      -> PuzzleDefinition 的關卡層級設定
nodes.csv       -> NodeDefinition
connections.csv -> ConnectionDefinition
obstacles.csv   -> ObstacleDefinition
```

完整 header：

```text
levels.csv
puzzle_id,title,start_node_id,total_length,minimum_slack_ratio,background_color,show_target_connections,vessel_color,base_width,tip_width,width_variation

nodes.csv
puzzle_id,node_id,label,x,y,radius,color

connections.csv
puzzle_id,from_node_id,to_node_id,point_count,thickness_scale,follow_delay_seconds,initial_direction_degrees

obstacles.csv
puzzle_id,obstacle_id,shape,center_x,center_y,width,height,radius,color
```

`PuzzleCatalogLoader` 採 staged build：先把四份文件讀到暫時結構，完成全部驗證後才建立 `PuzzleCatalog`。因此錯誤不會提交半套 catalog。診斷盡量指向來源檔、row 和 column／field。

### 4.1 CSV 語法

- Header 名稱、順序、欄位數必須完全一致。
- 支援 UTF-8 BOM、LF、CRLF、quoted field、quoted newline 與 doubled quote escape。
- 每一列的欄位數必須和 header 相同。
- ID、title、label 使用可顯示 ASCII；顏色是 `#RRGGBB` 或 `#RRGGBBAA`。
- 所有數值必須 finite，並通過擁有語意的範圍檢查。

### 4.2 關卡 preflight

Loader 驗證：

- puzzle、node、obstacle ID 唯一，跨表引用存在。
- 每關至少有節點和一條 connection，`start_node_id` 存在。
- node 圓和 obstacle 完整位於 1280×720；node 不互相重疊，也不和 obstacle 重疊。
- obstacle 只有 `rectangle` 或 `circle`；未使用的尺寸欄位必須留空。
- connection 不是自連線或重複 edge，`point_count` 為 8–12，style／timing 合法。
- 所有 connection 都能從 `start_node_id` 到達；候選圖允許多條 outgoing 與 incoming。
- 候選圖必須是 DAG，且只有一個可達 terminal node；不接受 cycle、重複 edge、斷開的 connection 或多終點。
- 每段 source → target 的直線不被 obstacle 阻擋。

障礙測試不是只看中心線。每段使用：

```text
clearance = 0.5 * max(base_width, tip_width) * thickness_scale + pixel_grid_size
```

把 rectangle／circle 向外擴張後，再做 segment-AABB 或 segment-circle；接觸邊界也視為阻擋。

最後檢查最短可完成條件：

```text
shortest_path_sum(distance(from, to)) * minimum_slack_ratio <= total_length
```

只要求至少一條起點到終點的路線能在總長度內完成；未選擇的候選 edge 不消耗預算。Loader 保留 CSV connection row order，但流程不依賴該順序。

## 5. PuzzleBoard：規則與不變量

`PuzzleBoard` 擁有目前關卡的 runtime state：目前末端節點、已選路徑、已固定 `Segment`、可選的 `Preview`、已花費長度和 solved flag。它不持有 renderer object。

永遠必須維持：

```text
committedLength + reservedLength + remainingLength = totalLength
```

- `committedLength`：成功段落在放開當下實際拉出的長度總和。
- `reservedLength`：目前 preview 暫時占用的長度；沒有 preview 時為 0。
- `remainingLength`：仍可使用的全域預算。

這裡沒有「每個節點的總長度」。每關只有 `PuzzleDefinition::totalLength`；每段都是從這筆值扣除。

### 5.1 狀態轉換

```text
Idle at active source
  -- left press on source --> Dragging preview

Dragging
  -- release on correct reachable target --> Committed segment
  -- release elsewhere / cancel ----------> Retracting preview

Retracting
  -- about 0.22 s --> preview removed, reservation fully returned

Committed segment
  --> chosen target becomes the only active source
  --> target has no outgoing candidate: solved
```

只有目前末端節點可開始拖曳。放開時，游標所在節點必須是該末端的一個 outgoing target；成功後只提交被選中的 edge，不能跳過節點，也不能回到已完成節點再拉第二條血管。

### 5.2 拖曳與消耗

每幀計算：

```text
desired = distance(source, pointer) * minimumSlackRatio
reserved = max(previousReserved, clamp(desired, 0, availableForThisConnection))
```

游標進入任一合法候選 target 時，保留量至少提升到該段的 `distance(source,target) * minimumSlackRatio`。`max(previousReserved, ...)` 使拖回不縮短，允許玩家主動留下鬆弛，但也能因前段花太多而讓後段不可達。

Commit 條件同時是：

1. pointer 位於目前末端的一個候選 target node 圓內。
2. reserved length 足以到達 target。
3. source-target 線段通過含血管半寬的 obstacle clearance test。

Commit 後 preview 的 reserved length 成為該 `Segment::committedLength`，tip 附著 target；沒有逐段 undo。`R`、離開關卡或重新選關會重建整個 `PuzzleBoard`。

`PuzzleBoardSnapshot` 是 renderer 的唯讀邊界，包含 tentacle points/style、total／remaining／reserved、active node、drag／retract／solved／lengthExhausted flags。`GetVisitedNodeIndices()` 與 `GetCommittedConnectionIndices()` 另外提供已選路徑的唯讀接口，讓後續團隊加入器官計分；Board 本身不解讀器官名稱也不計分。

## 6. BloodTentacle 模擬

每個粒子保存 `position` 和 `previousPosition`。`BloodTentacle` 只接收 root、tip target／anchor、可部署長度和 settings，不知道 node、puzzle 或 CSV。

### 6.1 每幀算法

```text
accumulate frame delta
repeat at most 8 times:
  fixed step = 1 / 120 second
  integrate movable points with Verlet + acceleration + damping
  pin root / update tip mode
  repeat 6 constraint iterations:
    correct adjacent point distance
    pin anchors again
```

預設 acceleration 是畫面向下的重力，固定兩端且 rest length 大於直線距離時會自然下垂。`SetDeployedLength` 改變鏈的總 rest length，平均分配到相鄰粒子距離；它不改變關卡預算。

Tip mode：

- `Free`：tip 參與一般 Verlet 運動。
- `FollowingTarget`：拖曳 preview；短 target history 依 `followDelaySeconds` 形成拖尾。
- `Attached`：tip 固定在任意 2D anchor。

root 永遠 pin 在 `rootAnchor`。若 attached anchor 超出可部署長度，tip 仍維持在 anchor，約束會形成張力，並透過 `TentaclePullOutput { desiredRootDisplacement, tension01, active }` 報告 root correction。本遊戲節點固定，不套用這個 displacement；這個 API 只保留模組的可重用性。

frame delta 會在上層 clamp，fixed step 又限制每幀最多 8 substeps，避免失焦或 breakpoint 後一次追算過多。Paused 不呼叫 board simulation，因此所有已固定與 preview 血管都凍結。

## 7. Ribbon 與 2D Rendering

`BuildRibbonStrip(points, style)` 是純 CPU 函式。它為每個中心點估算 tangent 和 normal，輸出 left/right 兩個 `RibbonVertex`：

```text
point 0 -> left0, right0
point 1 -> left1, right1
...
vertex count = 2 * pointCount
```

寬度會在 `baseWidth` 與 `tipWidth` 間插值，並可疊加固定 phase 的 sine variation；兩個寬度相等時沒有整段漸縮，variation 只向輪廓內側切出局部凹缺，不會讓可見血管超過宣告寬度。內建關卡使用 16 像素外輪廓、`0.16` variation 和深紅色，`TentacleStyle::pixelGridSize=2` 會把 ribbon 頂點量化到 2 像素格點。障礙 clearance 另保留一個完整 pixel cell，涵蓋量化造成的位移。模組仍保留 taper、variation 與關閉格點量化的能力。phase 不使用逐幀亂數，因此畫面穩定；重合點與急彎使用 fallback direction，避免 NaN。

`PuzzleRenderer` 使用一條 flat-color DirectX 12 pipeline：一般形狀使用 triangle list，血管使用 `D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP`。每條血管先畫深色外輪廓，再畫向內縮 4 像素的深紅 core，最後沿中心線以固定 hash 放置 2×2／4×2 的亮紅與暗紅像素斑塊。這些細節只讀取模擬點，不回寫或改變 Verlet 動作。Renderer 只把每幀 CPU 頂點複製到預先配置的 upload buffer，不擁有模擬狀態。

繪製順序固定：

```text
background
-> optional target hint lines
-> tentacle dark outlines
-> tentacle crimson cores
-> deterministic flesh pixels
-> rectangle / circle obstacles
-> nodes and active-source pulse
-> ASCII HUD / menu overlay
```

Obstacle 在血管後面畫，遮住 Verlet 中偶爾穿越障礙的視覺部分。這不是物理碰撞；玩法只使用 source-target 直線的 LOS 驗證。

HLSL 位於 `NoviceResources/shaders/Flat2DVS.hlsl` 和 `Flat2DPS.hlsl`，建置後由 `Resources/shaders/` 載入。UI 使用 KamataEngine 的 sprite／debug text 畫選單、HUD 和 overlay，不受血管 triangle strip topology 影響。

KamataEngine 啟動時會無條件初始化 Model 與 AxisIndicator，因此 `Obj*.hlsl` 和 `NoviceResources/axis/` 雖不屬於本作 2D renderer，仍是必要的 engine bootstrap 資源。CMake 會在 configure 階段檢查它們，避免只在執行時才發現缺檔。

## 8. GameFlow 與 session 生命週期

`GameFlow` 是不依賴 engine 的小型狀態機。它只把 navigation、confirm、escape、focus 和 hovered item 轉為 `GameCommand`；`Game` 執行 command。

| Screen | 選項／行為 |
| --- | --- |
| MainMenu | `PLAY`、`EXIT` |
| LevelSelect | catalog 中全部 puzzle、`BACK` |
| Playing | board input；Esc／失焦進 Paused |
| Paused | `RESUME`、`LEVEL SELECT`、`MAIN MENU`、`EXIT GAME` |
| Solved | 非最後關：`NEXT PUZZLE`、`LEVEL SELECT`、`RETRY`；最後關：`LEVEL SELECT`、`RETRY` |

沒有 fade 或 transition manager，screen 直接切換。`Game::Impl` 擁有：

- 一份啟動時載入、之後只讀的 `PuzzleCatalog`。
- 一個 `GameFlow`。
- 一個可選的 current puzzle index。
- 一個 `unique_ptr<PuzzleBoard>` active session。
- Input 與兩個 renderer adapter。

開始／Retry 建立新的 board；回 LevelSelect／MainMenu 會銷毀 board。Solved 保留 board 供畫面繼續顯示，0.6 秒後才開放完成選單輸入。

## 9. 初始化與每幀流程

### 9.1 初始化

```text
WinMain
  -> Application::Run(Game)
  -> working directory = executable directory
  -> KamataEngine::Initialize(1280, 720)
  -> Game::Initialize
       1. load + validate four CSV files
       2. initialize InputSystem
       3. initialize PuzzleRenderer and flat-color pipeline
       4. initialize GameUiRenderer
       5. start at MainMenu without active board
  -> frame loop
```

初始化採 RAII 與 staged ownership。失敗時不進 frame loop；結束順序是先釋放 UI／renderer／input 與 Game，再 finalize KamataEngine。

### 9.2 Playing frame

```text
InputSystem::Sample
  -> R retry shortcut
  -> focus / Esc handling and GameFlow
  -> BoardPointerInput(position, leftPressed, leftHeld, leftReleased)
  -> PuzzleBoard::Update
       update committed tentacles
       update/create/commit/retract preview
       detect solved
  -> PuzzleBoard::MakeSnapshot
  -> PuzzleRenderer::Draw
  -> GameUiRenderer::Draw
```

只有 stable `Playing` frame 會把 pointer input 交給 board。Paused 不推進 simulation；失焦前先立即取消 preview，再進 Paused。Solved 期間只保留完成畫面所需狀態與延遲，不再接受 board input。

## 10. 測試界線

`Object_Connect_CoreTests` 連結 `object_connect_core`，不依賴 KamataEngine 或 GPU。Suite 依責任分為：

- Data：CSV syntax、header／型別／顏色／shape、ID、cross-reference、path topology、正式四表整合。
- Geometry：segment-circle、segment-AABB、clearance、邊界接觸。
- Puzzle：只允許 active source、錯 target、障礙、保留／退還／commit、總長度不變量、solved。
- Tentacle：固定段長、重力下垂、root／tip pin、follow delay、max reach、pull output。
- Ribbon：`2 * pointCount`、像素格點、局部凹凸與粗細漸變、穩定 variation、degenerate input 不產生 NaN。
- Game：主選單、動態選關、暫停四項、Retry、Solved 與 Next。

每個 configuration 都要分別通過 `/W4 /WX` build 與 CTest。Headless tests 不能取代實機檢查：拖曳手感、下垂、提示線、障礙遮擋、triangle strip 裂縫、HUD 排版、游標與失焦暫停仍需人工回歸。

## 11. 新人修改指南

### 新增關卡

1. 在 `levels.csv` 新增 puzzle 設定。
2. 在 `nodes.csv` 放置節點。
3. 在 `connections.csv` 建立從 `start_node_id` 出發的唯一有向鏈。
4. 視需要在 `obstacles.csv` 加 rectangle／circle；沒有障礙的 puzzle 可以沒有對應列。
5. 建置後跑 CTest，再啟動遊戲確認手感與畫面。

不要在 `Game.cpp` 寫死新關卡，也不要依賴 connection CSV 的 row order。

### 修改規則

先判斷 ownership：

- 長度、解鎖、拖曳成功條件：`PuzzleBoard`。
- 粒子移動與約束：`BloodTentacle`。
- ribbon 幾何：`RibbonStrip`。
- 畫面顏色、layer、GPU state：Rendering。
- 選單狀態：`GameFlow`。
- 資料合法性：`PuzzleCatalogLoader`。

規則改在 core 後先補 contract test；不要讓 renderer 回呼 gameplay，也不要讓 `BloodTentacle` 讀 CSV 或知道 node ID。

### 擴充障礙

目前 obstacle 只影響資料 preflight 與 commit LOS，沒有粒子 collision。若要新增 polygon 或真正的血管繞障礙，必須先決定資料格式、clearance、solver 穩定性和玩法語意；這不是在 renderer 多畫一個形狀就能完成的局部修改。

## 12. 非目標與已知界線

目前刻意不支援：

- JSON、存檔、解鎖進度、熱重載或 localization。
- 音訊、血管 texture、sprite-sheet tentacle animation。
- Verlet 粒子對 obstacle 的 physical collision。
- 可移動 body、creature controller 或完整 CARRION 生物系統。
- ECS、scene graph、full physics、generic asset pipeline 或跨平台 renderer。
- 可調解析度；所有資料、input hit-test 和 UI 使用固定 1280×720 邏輯座標。

`TentaclePullOutput` 已能描述 attached tip 對 root 的拉力，但本作節點固定，沒有 consumer。保留這個輸出是 reusable module 的界線，不代表已實作 creature movement。
