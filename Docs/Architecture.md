# Object_Connect 架構

## 1. 設計目標

`Object_Connect` 是固定 1280×720 的 2D Game Jam 解謎 MVP。目前架構集中處理六件事：

1. 從三層 CSV 載入關卡與節點資料。
2. 在 runtime 依玩家拖曳建立連線；資料只描述節點，不預先列出 edge。
3. 管理多個 root／follow／end／dead 節點、容量和雙層長度預算。
4. 用 engine-independent Verlet 粒子鏈模擬每條已提交或預覽中的血管。
5. 用 KamataEngine／DirectX 12 把唯讀 snapshot 畫成簡單的 2D 畫面。
6. 將 UTF-8 UI 文字 layout 成 glyph，lazy rasterize 至 atlas 後以 DirectX 12 quad 繪製。

容易閱讀比高度泛化重要。專案沒有 ECS、service locator、scene graph、script VM 或通用 serialization framework。公開 API 位於 `include/ObjectConnect/`，實作固定放在對稱的 `src/ObjectConnect/`。平台與 GPU 細節只留在 runtime target。

## 2. Target 與依賴方向

```text
Object_Connect executable
  -> object_connect_runtime
       -> Application / Audio / Input / Game / UI / PuzzleRenderer / FontSystem
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
| Audio | 四個 WAV 一次載入、intro→loop phase、line hold／relax、duck fade | 引擎全域生命週期或遊戲事件判定 |
| Math | `Vec2`、`Color` 與小型數學 helper | engine adapter |
| Data | CSV 語法、三層 schema、路徑／欄位驗證、catalog 暫存後提交 | Runtime connection 或渲染 |
| Geometry | point／segment 對 AABB 的純函式 query | Verlet collision response 或 pathfinding |
| Tentacle/BloodTentacle | 一條模擬段落的粒子、fixed-step Verlet、tip mode、root pull output | 節點容量與長度預算 |
| Tentacle/RibbonStrip | 中心點到 ribbon triangle-strip 頂點 | GPU buffer 或 draw call |
| Puzzle/PuzzleBoard | Active node、dynamic connection、preview、commit、容量、雙層預算、solved | 選單、檔案 I/O、D3D、分數 |
| Game/GameFlow | MainMenu／LevelSelect／Playing／Paused／Solved 的輸入語意 | Board simulation 與繪製 |
| Input | 鍵盤、滑鼠與 focus edge | 解釋拖曳或選單 command |
| Rendering | Flat-color 血管／背景、快取節點 sprite、fallback、HUD、overlay、menu hit-test | 修改 Board 或 Flow |
| Text/FontSystem | strict UTF-8 decode、TTF metrics、layout、glyph／atlas cache、queue 與 generic quad draw | 翻譯、IME、fallback chain 或複雜 shaping |
| Game | Catalog、session、flow、next-level ID 與 renderer 的高層協調 | 低層 constraint 或 CSV parsing |

## 4. 三層資料模型

原始資料位於 `NoviceResources/data/`，建置後整份同步到 `Resources/data/`：

```text
levels.csv
nodes.csv
maps/<level_id>.csv
```

### 4.1 Level catalog

`levels.csv` 的完整 header：

```text
level_id,level_name,map_path,next_level_id,total_length,minimum_slack_ratio,background_color,vessel_color,base_width,tip_width,width_variation,wrap_edges
```

每列建立一個 `PuzzleDefinition`。列順序保留並直接成為 Level Select 順序。`map_path` 指向該關的 instance map；`next_level_id` 是 optional ID，不表示下一列。

`wrap_edges` 使用 `0`／`1`，空值或缺少欄位時預設為關閉；非法值會產生一次性啟動警告並以關閉處理。為相容既有資料，Loader 同時接受末尾沒有 `wrap_edges` 的舊 11 欄 header；其他欄位名稱或順序仍採嚴格驗證。

Loader 不要求 `next_level_id` 一定存在。`Game` 顯示 Solved 選單時會呼叫 `PuzzleCatalog::Find`；只有非空且確實存在的 ID 才產生 `次のステージ`，並依該 ID 啟動關卡。未知 ID 和空值都視為沒有下一關。

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

`lengthExhausted` 只在沒有 preview 的穩定狀態計算。它表示仍存在結構上可用的 source-target 配對，但 global/local 可部署長度不足以到達任何一個合法 target。關閉 `wrap_edges` 時維持既有直接路徑判定；開啟時會以相同的 wrapped-path 與 dead-node 規則檢查目標的 3×3 相鄰 image。這只是可達性提示，不會替實際拖曳選擇 winding 或自動提交穿越連線。

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
-> procedural bone-gray dead bodies and deterministic speckles
-> active halo and available-source pulse
-> root / follow / end sprites or rectangle fallbacks
-> GameUiRenderer UTF-8 HUD / menu overlay
```

Bundled root／follow／end 固定為 3×3 tiles＝48×48px，但 loader 仍可處理其他尺寸。Inactive node 使用 55% 亮度，active node 使用原貼圖／正常 fallback palette 並有固定低透明度 halo。`availableSource` 另有脈動提示；目前拖曳的 selected source 使用更亮、更快的 pulse。halo 只依 `active`，不依 `availableSource`。沒有 placement 的 node 不畫。

`Game::StartPuzzle` 在提交新 session 前呼叫 `PuzzleRenderer::PreparePuzzle`。Renderer 為當前關卡的每個 unique 非空 `texturePath` 建立一個 texture handle，依 node index 建立 sprite，並以 tile 尺寸與左上 placement 設定位置；切關時重建 sprite、沿用新舊關卡共有的 handle，並在舊 sprite 釋放後 release 舊關獨有的 handle。`PuzzleRenderer` 與 `GameUiRenderer` 共用 process-local 引用計數 registry，避免共享路徑被其中一方提早 unload。Registry 管理上限是 512 個 unique path，每關則限制為 255 個有 placement 的 unique node texture path；因此 old/new level 加 UI 能在 transactional prepare 期間同時存活，失敗時保留原 renderer 與 board。每幀 Draw 不做 Load/Create。空路徑使用 flat-color rectangle 加 outline；renderer finalize 會釋放仍屬於當前關卡的 sprite 與 handle。

Dead 不使用 texture；先畫骨灰底 `#625F58` 與深灰輪廓 `#302E2C`，再以 node ID／tile 座標的固定 hash 在約 35% tiles 畫 4×4 灰白或深灰斑點。base body 優先保留，vertex 不足只截斷斑點。這些視覺不改 dead placement、尺寸、AABB、clearance 或阻擋判定，也不能取代 particle collision。

UI 的 panel／card 仍使用 KamataEngine sprite，文字則全部 queue 到 `FontSystem`。`displayName` 空白或 node 沒有 placement 時不畫名稱；名稱放在 node 下緣 +4px，18px、置中／Top 對齊，以 1px 深色 shadow 分離貼圖；active 為暖白、inactive 為灰白。HUD 顯示 global remaining，不顯示每個 source 的 local remaining。Level Select 只使用既有 `white1x1.png` 染色疊出棕色框板、暖白關卡牌、左右箭頭與 BACK，不新增鎖關狀態；正式標題改為 41px、畫面置中的 `ステージ選択`。每頁固定顯示 5欄×2列，Draw、HitTest 與 cursor action hit 共用 layout；只有可翻頁方向的箭頭是有效 action。

### 11.1 FontSystem pipeline 與 ownership

文字資料流固定如下，generic quad backend 不知道 UTF-8、TTF 或 glyph 的語意：

```text
UTF-8 bytes
-> strict decoder
-> Unicode scalar values
-> layout cache
-> glyph cache lookup
-> stb_truetype metrics / rasterization
-> R8_UNORM atlas region
-> generic textured quads
-> DirectX 12
```

Decoder 拒絕 overlong encoding、surrogate、超過 U+10FFFF 與截斷序列，依 maximal-subpart 規則產生 U+FFFD 且保證前進；newline 接受 LF、CRLF 和 bare CR。Layout 使用 font ascent、descent、line gap、advance、bearing 與簡單 kerning；空白只移動 pen，不配置 atlas。找不到 code point 時先使用 U+FFFD，該字型也沒有 U+FFFD 時才退到 glyph 0，並以 `{font, pixelSize, codePoint}` 為單位只警告一次。

每個 font record 擁有自己的 glyph cache 與一組 1024×1024 `R8_UNORM` atlas page。第一頁在 load 時建立，以便立即回報 GPU resource failure；後續頁面只在舊頁填滿時 lazy 建立。Shelf packing 在 glyph 周圍保留 1px 透明 padding；已配置的 rect 永不搬移或覆寫。每頁使用持續 mapped、依 `GetCopyableFootprints` 配置的 upload buffer，只在 glyph 首次出現時提交 dirty-region copy。完整 layout 使用 256-entry LRU，queue 每 frame 上限 4096 glyph；statistics 分別追蹤 decode、layout hit／miss、rasterization、atlas upload 與 missing glyph，讓 cache 行為可被驗收。

`FontHandle` 只代表字型檔，pixel size 留在 Measure／Draw。相同正規化路徑共用 record 並增加引用計數；ID 不重用，invalid／stale handle 會安全失敗。最後一個 unload 釋放該 font 的 glyph cache、atlas texture、upload buffer 和 descriptor heap；`Finalize` 可重複呼叫。這也使字型資源壽命不會滲入 `PuzzleBoard` 或 `GameFlow`。

實作只在一個 private translation unit 啟用 KamataEngine 隨附的 `imstb_truetype.h` 1.26 static implementation，不引入 FreeType、SDL_ttf、system font 或額外 DLL。TTF parser 的輸入邊界限定為遊戲打包、可信任的字型檔；不把玩家或網路提供的任意 TTF 視為安全輸入。目前只做簡單 glyph positioning，不支援 shaping、fallback font chain、IME、直書、Ruby 或 rich text。

`GameAudio` 是 runtime-only 的 KamataEngine Audio adapter。它在初始化時一次載入 `bgm_start.wav`、`bgm_loop.wav`、`line_hold.wav`、`line_relax.wav`，以 `Stopped／Intro／Loop` 管理音樂 phase。intro 結束後 polling 切入 loop；缺 intro 直進 loop，缺 loop 則 intro 後靜音，失敗不逐 frame 重試。成功進過關卡後返回 Main Menu 才重啟 sequence，其他場景切換不影響音樂。drag false→true 播一次 hold；真正 release 才播 relax，強制 cancel 只淡出 hold。BGM attack duck 為 50ms／0.4 gain，release 為 250ms；hold fade 為 50ms。`GameFlow` 與 `PuzzleBoard` 不持有音訊資源。

`MouseCursorRenderer` transactionally 載入三張 32×32 RGBA 圖。Playing drag 使用握拳，非 Playing 的有效 action 使用指向，其餘使用張手；hotspot 分別為中心、`(16,1)`、中心。只有 focused 且 pointer 位於 client 內才以成對 `ShowCursor` 呼叫隱藏 OS cursor，失焦、離開 client、初始化失敗與 Finalize 都恢復。cursor 在 `FontSystem::Flush` 後最後繪製。

## 12. GameFlow 與 session

`GameFlow` 是 engine-independent 小型狀態機。第三個參數是 `hasNextPuzzle`，不是「目前是否為 catalog 最後一列」。

| Screen | 選項／行為 |
| --- | --- |
| MainMenu | `ゲーム開始`、`終了` |
| LevelSelect | CSV 順序的全部 puzzle、`戻る` |
| Playing | Board input；Esc／失焦進 Paused |
| Paused | `再開`、`リトライ`、`ステージ選択`、`メインメニュー`、`ゲーム終了` |
| Solved | 有有效 next：`次のステージ`、`ステージ選択`、`リトライ`；否則只有後兩項 |

`Game` 是唯一高層組裝點。它擁有只讀 catalog、flow、optional current puzzle index、active `PuzzleBoard`、Input、audio adapter、`FontSystem` 和 puzzle／UI／cursor 三個 renderer。Start／Retry 建立新 Board；遊戲中的重開入口位於 Paused 選單；回選關／主選單銷毀 Board；Solved 保留畫面約 0.6 秒後才接受完成選單輸入。

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
       FontSystem::Initialize
       FontSystem::LoadFont(Resources/fonts/game.ttf)
       GameUiRenderer::Initialize
       MouseCursorRenderer::Initialize (失敗時保留 OS cursor)
       GameAudio::Initialize (intro 立即開始)
  -> frame loop
```

`GameConfig::uiFontPath` 預設為 `fonts/game.ttf`，相對於 runtime `Resources/` 解決。它是必要資產：檔案缺失或 TTF 無效時，`Game::Initialize` 失敗，診斷包含解決後的完整路徑。建置系統只負責把 `NoviceResources/fonts/game.ttf` 隨其他資源部署，不把它變成 configure-time 必要檔。

Playing frame：

```text
FontSystem::BeginFrame
InputSystem::Sample
  -> Retry / focus / Esc / GameFlow
  -> BoardPointerInput
  -> PuzzleBoard::Update
       update committed tentacles
       create/update/commit/retract preview
       update node state and solved
  -> MakeSnapshot
  -> PuzzleRenderer::Draw
  -> GameUiRenderer::Draw (queue text only)
  -> KamataEngine Sprite::PostDraw
  -> FontSystem::Flush
  -> MouseCursorRenderer::Draw
```

只有穩定的 Playing frame 把 pointer input 交給 Board。Paused 不推進模擬；失焦先取消 preview 再暫停。文字延後至所有 KamataEngine sprite 結束後 flush，確保 HUD 位於最上層，也避免 FontSystem 與 Sprite 在同一段 draw 中互相覆蓋 descriptor heap。`Flush` 只把 copy／barrier／draw 記錄到目前的 command list；`Game` 在 `DirectXCommon::PostDraw` 提交並完成該 frame 後，才可能 unload 或 finalize font GPU resources。

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
- UTF-8 ASCII／2／3／4-byte 與日文混排，以及非法 continuation、overlong、surrogate、超範圍和截斷輸入的 U+FFFD 行為。
- Synthetic font metrics 下的 advance、bearing、kerning、baseline、line height、空字串、CR／LF／CRLF、trailing newline 和 alignment。
- CSV 日文 `level_name`／`display_name` round-trip。
- Fake rasterizer／atlas seam 下的跨 frame glyph cache、font／size 隔離、layout hit、單次 upload、missing glyph 與 atlas failure。
- Monotonic font ID 在 registry lifetime 之間不重用，且耗盡時不 wrap。

另一個 headless lifecycle test 會連結 runtime，但不建立視窗或 D3D12 resources；它驗證未初始化／invalid handle、安全失敗、statistics 不被污染和重複 `Finalize`。獨立 fake-audio test 驗證 intro→loop、缺檔 fallback、restart、hold／release／cancel 與 duck fade；core test 驗證五種 screen 的 cursor priority、失焦與 client 外 visibility gate。Headless tests 仍不能驗證 GPU 畫面、實際音訊輸出、32×32 hotspot、雙 cursor、拖曳手感、dead 遮擋和 HUD 排版。實機驗收須確認 48×48 清晰度、名稱間距、halo、骨灰斑點、原 dead 解法與音訊 clipping，並在 DirectX 12 debug layer 下檢查 resource-state error 與 live-object leak。

## 15. 擴充界線

目前尚未實作：

- Sprite atlas 的 frame selection，以及 preset/map 的編輯器或回寫工具。
- Canvas bounds、node overlap 與整關可完成性 preflight。
- Dead 對 Verlet particles 的 collision、繞障礙或 pathfinding。
- 器官分數與直接路線彩蛋計分。
- 出血／血壓倒數。
- 存檔、解鎖、音訊設定、hot reload、翻譯／locale 切換或完整物理。
- Font shaping、fallback font chain、IME、直書、Ruby 與 rich text。

新增真正的 obstacle collision 不只是 renderer 改圖層：必須先定義 collision shape、preview 行為、constraint solver 穩定性和長度語意。Texture 載入與 sprite lifecycle 應繼續留在 Rendering；不要讓 `PuzzleBoard` 持有 GPU asset。
