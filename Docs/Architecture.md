# Object_Connect 架構

## 1. 設計目標

`Object_Connect` 是固定 1280×720 的 2D Game Jam 解謎 MVP。目前架構集中處理五件事：

1. 從三層 CSV 載入關卡與節點資料。
2. 在 runtime 依玩家拖曳建立連線；資料只描述節點，不預先列出 edge。
3. 管理多個 root／follow／end／dead 節點、容量和雙層長度預算。
4. 用 engine-independent Verlet 粒子鏈模擬每條已提交或預覽中的血管。
5. 用 KamataEngine／DirectX 12 把唯讀 snapshot 畫成簡單的 2D 畫面。

容易閱讀比高度泛化重要。專案沒有 ECS、service locator、scene graph、script VM 或通用 serialization framework。公開 API 位於 `include/ObjectConnect/`，實作固定放在對稱的 `src/ObjectConnect/`。平台與 GPU 細節只留在 runtime target。

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

依賴只往下：core 不 include KamataEngine、Win32 或 D3D 型別。Rendering 只讀 `PuzzleDefinition` 和 `PuzzleBoardSnapshot`，不修改玩法狀態。兩個 renderer 使用 PIMPL 隔離 engine ownership。

## 3. 模組責任

| 模組 | 擁有／負責 | 不負責 |
| --- | --- | --- |
| Core | KamataEngine lifetime、工作目錄、主迴圈、frame timing | 解謎規則或 draw data |
| Math | `Vec2`、`Color` 與小型數學 helper | engine adapter |
| Data | CSV 語法、三層 schema、路徑／欄位驗證、catalog 暫存後提交 | Runtime connection 或渲染 |
| Geometry | point／segment 對 AABB 的純函式 query | Verlet collision response 或 pathfinding |
| Tentacle/BloodTentacle | 一條模擬段落的粒子、fixed-step Verlet、tip mode、root pull output | 節點容量與長度預算 |
| Tentacle/RibbonStrip | 中心點到 ribbon triangle-strip 頂點 | GPU buffer 或 draw call |
| Puzzle/PuzzleBoard | Active node、dynamic connection、preview、commit、容量、雙層預算、solved | 選單、檔案 I/O、D3D、分數 |
| Game/GameFlow | MainMenu／LevelSelect／Playing／Paused／Solved 的輸入語意 | Board simulation 與繪製 |
| Input | 鍵盤、滑鼠與 focus edge | 解釋拖曳或選單 command |
| Rendering | Flat-color 血管／背景、快取節點 sprite、fallback、HUD、overlay、menu hit-test | 修改 Board 或 Flow |
| Game | Catalog、session、flow、next-level ID 與 renderer 的高層協調 | 低層 constraint 或 CSV parsing |

## 4. 三層資料模型

原始資料位於 `NoviceResources/data/`，建置後整份同步到 `Resources/data/`：

```text
levels.csv
nodes.csv
maps/<level>.csv
```

### 4.1 Level catalog

`levels.csv` 的完整 header：

```text
level_id,level_name,map_path,next_level_id,total_length,minimum_slack_ratio,background_color,vessel_color,base_width,tip_width,width_variation
```

每列建立一個 `PuzzleDefinition`。列順序保留並直接成為 Level Select 順序。`map_path` 指向該關的 instance map；`next_level_id` 是 optional ID，不表示下一列。

Loader 不要求 `next_level_id` 一定存在。`Game` 顯示 Solved 選單時會呼叫 `PuzzleCatalog::Find`；只有非空且確實存在的 ID 才產生 `NEXT PUZZLE`，並依該 ID 啟動關卡。未知 ID 和空值都視為沒有下一關。

### 4.2 Node preset catalog

`nodes.csv` 的完整 header：

```text
preset_id,node_type,texture_path,width_tiles,height_tiles,display_name,max_incoming,max_outgoing,max_outgoing_length
```

`NodePresetCatalogLoader` 可獨立產生 `NodePresetCatalog`。遊戲使用的 `PuzzleCatalogLoader` 也讀取相同檔案，並在解析各關 map 時取得 resolved node。

有 `source_preset_id` 的 row 先複製 preset 的 type、texture、尺寸、名稱與容量，再以 map 中非空白欄位覆寫。未知 preset ID 是資料錯誤；這個 merge 發生在載入期，不會修改或回寫 CSV。

### 4.3 Per-level map

每關 map 的完整 header：

```text
instance_id,source_preset_id,node_type,texture_path,width_tiles,height_tiles,display_name,tile_x,tile_y,max_incoming,max_outgoing,max_outgoing_length
```

每列直接建立一個 `NodeDefinition`：

- `source_preset_id` 可留空；非空時必須引用既有 preset。
- `node_type` 是 `root`、`follow`、`end` 或 `dead`；有 preset 時可空白繼承，沒有 preset 時必填。
- `tile_x`／`tile_y` 必須同時存在或同時留空。
- 一格固定為 16×16 邏輯像素。
- `width_tiles`／`height_tiles` 與 placement 形成 node AABB。
- 沒有 placement 的 node 保留在 definition 中，但 Board 將它視為不可顯示、不可互動。
- type 以外的可覆寫欄位也以「非空白才覆寫」處理。有 preset 時，空白的 texture、尺寸、名稱和容量都繼承 preset；沒有 preset 時則保留 `NodeDefinition` 的預設值。
- 空白不能清除 preset 已設定的 `texture_path` 或 `display_name`。需要 resolved 空值時，應引用該欄原本就是空白的 preset，或不指定 preset。
- resolved `texture_path` 非空時 file loader 會確認資源存在；resolved `display_name` 空白時 UI 不畫名稱。

### 4.4 Loader 邊界

`PuzzleCatalogLoader::Load` 讀取 `levels.csv` 與 `nodes.csv`，再載入各列引用的 map；相同 map path 只讀一次。`Parse` 版本接收 levels、nodes 文字和一組 `{path, contents}` map sources，供 headless tests 使用。

兩個 catalog loader 都先建立暫時結果，成功後才覆寫 caller 的 catalog。錯誤不會留下 partial data。共同的 CSV reader 支援 BOM、LF／CRLF、quoted field、quoted newline 和 doubled quote escape；schema header 名稱與順序必須完全相同。ID 使用 lower_snake_case，路徑必須留在 resource root 內，診斷包含來源與欄位位置。

目前 loader 驗證 schema、欄位型別、基本數值、路徑和引用檔案存在；它尚未做完整的畫布邊界、node overlap 或可完成性 preflight。

## 5. Node runtime model

四種 type 的能力由 Board 固定解讀：

```text
Root   : source，可在初始化時 active
Follow : target；接入後 active，也可成為 source
End    : target；不能成為 source
Dead   : 不是 source/target；作為矩形 LOS blocker
```

初始化時，所有「有 placement 的 root」都會 active，因此一關可有多個起點。Follow 被任一成功連線接入後保持 active；只要容量和長度仍足夠，任何 active root／follow 都可再次被選為 source。

每個 node 的 mutable state 很小：

```text
active
incomingUsed
outgoingUsed
committedOutgoingLength
```

`max_incoming`／`max_outgoing` 限制線的數量；`max_outgoing_length` 限制該 source 累積花費的長度。這允許資料設計分支與合流，不存在唯一 current tip 或固定路徑。

## 6. Dynamic connection 規則

Map 不保存 edge。玩家在 runtime 從 source 拖向 target，Board 依目前 committed graph 判斷。

Source 必須：

- 有 placement 且 active。
- Type 為 root 或 follow。
- `outgoingUsed < maxOutgoing`。
- 全域與 node-local 長度都大於 0。

Target 必須：

- 有 placement。
- Type 為 follow 或 end。
- `incomingUsed < maxIncoming`。

Commit 另外拒絕：

- self connection。
- 已存在的同方向 duplicate edge。
- 會讓 committed graph 形成 cycle 的 edge。
- 長度不足。
- 被任一 placed dead AABB 阻擋的直線。

成功後建立 `CommittedLine { fromNodeIndex, toNodeIndex, committedLength }`，增加 source outgoing、target incoming，並啟動 target。Board 同時保留一個 attached `BloodTentacle` segment 作為畫面模擬。

### 6.1 Solved 條件

`EvaluateSolved` 只檢查有 placement 的 end：至少要有一個，而且所有這些 end 都必須 active。沒有 placement 的 end 不參與；沒有任何 placed end 的關卡不會自動完成。

## 7. 雙層長度預算

全域不變量：

```text
committedLength + reservedLength + remainingLength = totalLength
```

Node-local 不變量（目前 preview 來自該 source 時）：

```text
node.committedOutgoingLength
+ reservedLength
+ nodeRemainingOutgoingLength
= node.maxOutgoingLength
```

Preview 的最大可部署長度是：

```text
min(globalRemainingLength, sourceRemainingOutgoingLength)
```

拖曳時計算：

```text
desired = distance(sourceCenter, pointer) * max(1, minimumSlackRatio)
reserved = max(previousReserved, clamp(desired, 0, previewMaxLength))
```

因此拖遠後再回來不會自動釋放長度。Commit 時 reserved 成為固定線段的 rest length；取消或失敗時進入約 0.22 秒 Retracting，reserved 隨收回退還。失焦／暫停前的立即取消則直接移除 preview。

`lengthExhausted` 只在沒有 preview 的穩定狀態計算。它表示仍存在結構上可用的 source-target 配對，但 global/local 可部署長度不足以到達任何一個合法 target。

## 8. Dead tip clamp、LOS 與 Geometry

Dead node 的 placement 和 tile size 形成 `AxisAlignedBox`。拖曳與 commit 都使用相同 clearance：

```text
clearance = 0.5 * max(vessel baseWidth, vessel tipWidth)
expandedDead = ExpandAxisAlignedBox(deadBounds, clearance)
blocked = SegmentIntersectsAxisAlignedBox(sourceCenter, targetCenter, expandedDead)
```

拖曳時，`SegmentAxisAlignedBoxEntryTime` 找出 source-center 到游標之間最早進入 expanded dead 的時間，preview tip 會退到該接觸點前；commit 時再以 source-center 到 target-center 做完整 LOS 驗證。邊界接觸算相交。

這不是完整物理碰撞：只有 following tip target 被 clamp，中間 Verlet particles 和已固定段落仍可能因擺動或下垂穿進 dead，也沒有自動繞行或 pathfinding。

Geometry 模組目前只提供 AABB 所需的純函式：validation、point containment、expand、segment entry time，以及 single/multiple segment intersection。

## 9. Snapshot 邊界

`PuzzleBoardSnapshot` 是 Rendering 的唯讀 contract：

```text
tentacles
nodeStates[] { drawable, active, availableSource,
               incomingUsed, outgoingUsed, committedOutgoingLength }
selectedSourceNodeIndex
totalLength / remainingLength / reservedLength
dragging / retracting / solved / lengthExhausted
```

`availableSource` 是 Board 對 active、type、容量和 global/local remaining 的統一判斷。Renderer 不重新實作規則，只用它顯示 pulse。`selectedSourceNodeIndex` 在 preview 存在時標示這次拖曳的 source。

額外唯讀 API `GetActivatedNodeIndices()`、`GetCommittedLines()` 和 `GetRemainingOutgoingLength()` 可供未來 HUD、統計或分數系統使用；Board 本身不計算器官分數。

## 10. BloodTentacle 與 Ribbon

每個 particle 保存 `position` 和 `previousPosition`。`BloodTentacle` 只知道 root、tip target／anchor、最大與已部署長度和 settings，不知道 node ID 或 CSV。

```text
accumulate frame delta
repeat at most 8 times:
  fixed step = 1 / 120 second
  Verlet integrate movable points
  pin root / apply tip mode
  repeat 6 constraint iterations:
    correct adjacent distance
    pin anchors again
```

Board 目前為每條 runtime connection 使用 10 points 和 0.03 秒 follow delay。成功後 tip attached 在 target center；root attached 在 source center。`TentaclePullOutput` 仍保留給模組重用，但本遊戲節點固定，不套用 root displacement。

`BuildRibbonStrip` 每個中心點輸出 left/right 兩個 `RibbonVertex`，所以 vertex count 為 `2 * pointCount`。寬度、variation、phase 和顏色來自關卡 style；degenerate point 使用 fallback direction，避免 NaN。

## 11. 目前 Rendering

`PuzzleRenderer` 使用 flat-color DirectX 12 pipeline 畫背景、血管、pulse 與 fallback；有 `texturePath` 的節點交由 KamataEngine `Sprite` 繪製。一般矩形／圓形用 triangle list，血管用 triangle strip。

固定順序：

```text
background
-> tentacle dark outlines
-> tentacle crimson cores
-> deterministic flesh pixels
-> dead node sprites or rectangle fallbacks
-> available-source pulse
-> root / follow / end sprites or rectangle fallbacks
-> GameUiRenderer ASCII HUD / menu overlay
```

Inactive node 使用灰暗 tint，active node 使用原貼圖／正常 fallback palette。`availableSource` 有脈動提示；目前拖曳的 selected source 使用更強的 pulse。沒有 placement 的 node 不畫。

`Game::StartPuzzle` 在提交新 session 前呼叫 `PuzzleRenderer::PreparePuzzle`。Renderer 為當前關卡的每個 unique 非空 `texturePath` 建立一個 texture handle，依 node index 建立 sprite，並以 tile 尺寸與左上 placement 設定位置；切關時重建 sprite、沿用新舊關卡共有的 handle，並在舊 sprite 釋放後 release 舊關獨有的 handle。`PuzzleRenderer` 與 `GameUiRenderer` 共用 process-local 引用計數 registry，避免共享路徑被其中一方提早 unload。Registry 管理上限是 512 個 unique path，每關則限制為 255 個有 placement 的 unique node texture path；因此 old/new level 加 UI 能在 transactional prepare 期間同時存活，失敗時保留原 renderer 與 board。每幀 Draw 不做 Load/Create。空路徑使用 flat-color rectangle 加 outline；renderer finalize 會釋放仍屬於當前關卡的 sprite 與 handle。

Dead 的 sprite／fallback 畫在血管之後，只能視覺遮住部分穿越，不能取代 particle collision。

UI 使用 KamataEngine sprite 和 debug text。`displayName` 空白或 node 沒有 placement 時不畫名稱；HUD 顯示 global remaining，不顯示每個 source 的 local remaining。

## 12. GameFlow 與 session

`GameFlow` 是 engine-independent 小型狀態機。第三個參數是 `hasNextPuzzle`，不是「目前是否為 catalog 最後一列」。

| Screen | 選項／行為 |
| --- | --- |
| MainMenu | `PLAY`、`EXIT` |
| LevelSelect | CSV 順序的全部 puzzle、`BACK` |
| Playing | Board input；Esc／失焦進 Paused |
| Paused | `RESUME`、`LEVEL SELECT`、`MAIN MENU`、`EXIT GAME` |
| Solved | 有有效 next：`NEXT PUZZLE`、`LEVEL SELECT`、`RETRY`；否則只有後兩項 |

`Game` 是唯一高層組裝點。它擁有只讀 catalog、flow、optional current puzzle index、active `PuzzleBoard`、Input 和兩個 renderer。Start／Retry 建立新 Board；回選關／主選單銷毀 Board；Solved 保留畫面約 0.6 秒後才接受完成選單輸入。

## 13. 初始化與每幀資料流

```text
WinMain
  -> Application::Run
  -> executable directory 成為 working directory
  -> KamataEngine::Initialize
  -> Game::Initialize
       PuzzleCatalogLoader::Load(levels + referenced maps)
       InputSystem::Initialize
       PuzzleRenderer::Initialize
       GameUiRenderer::Initialize
  -> frame loop
```

Playing frame：

```text
InputSystem::Sample
  -> Retry / focus / Esc / GameFlow
  -> BoardPointerInput
  -> PuzzleBoard::Update
       update committed tentacles
       create/update/commit/retract preview
       update node state and solved
  -> MakeSnapshot
  -> PuzzleRenderer::Draw
  -> GameUiRenderer::Draw
```

只有穩定的 Playing frame 把 pointer input 交給 Board。Paused 不推進模擬；失焦先取消 preview 再暫停。

## 14. 測試界線

`Object_Connect_CoreTests` 只連結 core，不建立視窗或需要 GPU。測試責任包括：

- CSV syntax 與三種 schema／map source contract。
- Node preset 與 per-level resolved data。
- AABB containment、expand、segment intersection 與 boundary touch。
- Multiple roots、dynamic source／target、branch capacity、duplicate 與 cycle。
- Global/local budget、monotonic reserve、commit、refund、dead LOS 和 solved。
- BloodTentacle constraint、follow、attachment、pull output。
- Ribbon vertex contract 與 degenerate safety。
- MainMenu／LevelSelect／Pause／Solved，以及 `hasNextPuzzle` 選單差異。

Headless tests 不能驗證 GPU 畫面、拖曳手感、dead 遮擋和 HUD 排版，這些仍需人工回歸。

## 15. 擴充界線

目前尚未實作：

- Sprite atlas 的 frame selection，以及 preset/map 的編輯器或回寫工具。
- Canvas bounds、node overlap 與整關可完成性 preflight。
- Dead 對 Verlet particles 的 collision、繞障礙或 pathfinding。
- 器官分數與直接路線彩蛋計分。
- 出血／血壓倒數。
- 存檔、解鎖、音訊、hot reload、localization 或完整物理。

新增真正的 obstacle collision 不只是 renderer 改圖層：必須先定義 collision shape、preview 行為、constraint solver 穩定性和長度語意。Texture 載入與 sprite lifecycle 應繼續留在 Rendering；不要讓 `PuzzleBoard` 持有 GPU asset。
