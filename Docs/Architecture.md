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
| Game/GameFlow | MainMenu／LevelSelect／Playing／Paused／Solved／FinalResults 的輸入語意 | Board simulation 與繪製 |
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

每列建立一個 `PuzzleDefinition`。列順序保留並直接成為 Level Select 順序。`map_path` 指向該關的 instance map；`next_level_id` 是 optional ID，不表示下一列。保留值 `final_results` 代表該關通過後直接進入最終結算，不能作為真正的 `level_id`。

`wrap_edges` 使用 `0`／`1`，空值或缺少欄位時預設為關閉；非法值會產生一次性啟動警告並以關閉處理。為相容既有資料，Loader 同時接受末尾沒有 `wrap_edges` 的舊 11 欄 header；其他欄位名稱或順序仍採嚴格驗證。

Loader 不要求一般的 `next_level_id` 一定存在。`Game` 顯示 Solved 選單時會呼叫 `PuzzleCatalog::Find`；只有非空且確實存在的 ID 才產生 `次のステージ`，並依該 ID 啟動關卡。未知 ID 和空值都視為沒有下一關；精確值 `final_results` 則走獨立的最終結算流程，不會被當成地圖 ID。

最終結算只保留本輪實際通過的關卡，並依通關順序建立逐關結果卡。從 Level Select 啟動關卡會清空舊路線；經 `次のステージ` 前進會保留已完成關卡。每關在 solved 當下複製所有「有 placement 且 type 為 `root`、`follow` 或 `end`」的 node 狀態；runtime `active` 數量為 C，該卡納入的 node 總數為 T，顯示為 `【臓器 C/T】`。`dead` 與沒有 placement 的 hidden node 排除，heart／brain 也納入；所有結果卡統一使用 `assets/textures/node/organ.png`，不依個別 node texture 建立結算圖示。

結果頁每頁固定 10 張卡，進入時預設顯示本輪最後一頁；左右箭頭與滑鼠滾輪負責翻頁，首尾不存在的方向保持 disabled。卡片以 C/T 比例決定狀態色，只有 C=T 的全滿卡顯示金色邊框；不計算或顯示獎牌、S／A／B 等級。結果只存在目前 runtime session，不寫入 save 或跨次啟動持久化。

`final_results`、Next、Retry 與 reset 的既有語意不變：精確的 `next_level_id=final_results` 在該關記錄完成結果後直接進 FinalResults；Next 保留路線並繼續累計；Retry 移除 tracker 尾端的當前關舊結果，重新通關後再寫入；Level Select 或 Main Menu 會清空整輪結果。

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
| FinalResults | 以 10 張／頁顯示本輪逐關結果卡，預設最後頁並支援箭頭／滾輪；`ステージ選択`、`メインメニュー`，Esc 回 LevelSelect |

`Game` 是唯一高層組裝點。它擁有只讀 catalog、flow、optional current puzzle index、active `PuzzleBoard`、本次路線的完成紀錄、Input、audio adapter、`FontSystem` 和 puzzle／UI／cursor 三個 renderer。Start／Retry 建立新 Board；遊戲中的重開入口位於 Paused 選單；回選關／主選單銷毀 Board；普通 Solved 保留畫面約 0.6 秒後才接受完成選單輸入，`final_results` 則在 solved 當幀直接進 FinalResults。

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
       FontSystem::LoadFont(Resources/fonts/BIZUDPGothic-Regular.ttf)
       GameUiRenderer::Initialize
       MouseCursorRenderer::Initialize (失敗時保留 OS cursor)
       GameAudio::Initialize (intro 立即開始)
  -> frame loop
```

`GameConfig::uiFontPath` 預設為 `fonts/BIZUDPGothic-Regular.ttf`，相對於 runtime `Resources/` 解決。它是必要資產：CMake 在 configure 時確認原始字體存在，建置時將它隨 `NoviceResources/` 全部部署，打包時再檢查完整資源。檔案缺失或 TTF 無效時，`Game::Initialize` 仍會失敗，診斷包含解決後的完整路徑；建置期存在檢查不能代替 runtime TTF 解析。

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
- MainMenu／LevelSelect／Pause／Solved／FinalResults，以及 `hasNextPuzzle` 選單差異；逐關結果需驗證完成順序、Root／Follow／End active/total、排除 Dead／hidden、10 張分頁、最後頁預設與 reset。
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
- 結算卡以外的器官分數、獎牌階級與直接路線彩蛋計分。
- 出血／血壓倒數。
- 存檔、解鎖、音訊設定、hot reload、翻譯／locale 切換或完整物理。
- Font shaping、fallback font chain、IME、直書、Ruby 與 rich text。

新增真正的 obstacle collision 不只是 renderer 改圖層：必須先定義 collision shape、preview 行為、constraint solver 穩定性和長度語意。Texture 載入與 sprite lifecycle 應繼續留在 Rendering；不要讓 `PuzzleBoard` 持有 GPU asset。

## 16. 依賴封裝與 CI 發版

本節記錄設計與安全邊界；本機命令、commit 範例、版本選擇、tag 推送及失敗排查以 [README](../README.md#ビルドと実行) 為單一操作來源，避免分別維護兩套指令。

### 16.1 專案內 SDK 與工具鏈邊界

`third_party/KamataEngine/External/` 保存必要的預編譯 SDK 快照：KamataEngine 與 DirectXTex 完整標頭、各自 Debug／Release `.lib` 和對應 `.pdb`，以及 ImGui 標頭與現有授權文件。PDB 是開發連結／除錯資訊，不進入玩家 ZIP。引擎 `.git`、範例、Develop 配置、`.idb`、ImGui 原始專案及重複遊戲資源不屬於此快照。

依賴使用同一 repository 的普通 Git 版本化，不新增 submodule、LFS、下載腳本或依賴倉庫 token。隨快照記錄來源 revision、檔案 SHA-256 與已提供的授權資訊；不得將缺少授權文件解讀為自動取得新授權。更新 SDK 時應把標頭、各構成 library、配套 PDB、來源資訊和雜湊一起更新，並重新驗證四組建置，而非單獨替換一個二進位檔。

CMake 的引擎 root 固定由專案來源目錄推導，配置過程移除舊 `KAMATA_ENGINE_ROOT` cache entry，也不讀取 `KAMATA_ENGINE` 環境變數；Build／Run 不再接受 `-KamataEngineRoot`。因此既有機器的外部 `Runtime` 路徑不能偷偷覆蓋 repository 內的版本。舊 CLion profile 應 reload CMake 並移除外部路徑選項。

這個邊界只封裝 SDK，不封裝編譯工具。MSVC、對應 CMake、Windows SDK 仍由本機或 runner 提供；KamataEngine／DirectXTex 以 imported static targets 連結，不在本專案重編引擎。

| 配置 | MSVC runtime | DXC 部署 | 用途 |
| --- | --- | --- | --- |
| Debug | `/MDd`，配合預編譯 Debug library | Windows SDK x64 Redist 的 `dxcompiler.dll`、`dxil.dll` 放在 EXE 旁 | 開發／除錯，不作玩家配布 |
| Release | `/MT`，配合預編譯 Release library | 不部署上述兩檔，清除輸出中同名殘留 | 玩家 ZIP |

純 Ninja Release configure 不要求 DXC Redist；VS multi-config 同時含 Debug／Release，因此 configure 階段仍須滿足 Debug 的 DXC 檔案。Release 仍需要一般 Windows SDK，靜態 CRT 也不代表不依賴 Windows 系統 DLL。必備資源包括字體 `BIZUDPGothic-Regular.ttf`；來源 `NoviceResources/` 全量部署至 EXE 旁的 `Resources/`，由 configure、部署與 package 分別守住檔案存在、同步和完整性。

Build 預設 Release、Run 預設 Debug，使用者以 `-Configuration` 切換，無須改寫腳本。所有本機 presets 維持輸出 `target/<Configuration>/`；同一 checkout 不應並行建置會寫入相同目錄的不同 generator。

### 16.2 CI 驗證與打包匯合

`Windows build and release` workflow 的入口是 `master` push、以 `master` 為目標的 PR、手動執行、`v*` tag push，以及 GitHub Release 的 `published`。正式事件先取得同 tag 的 workflow-level concurrency 鎖，再由前置準備 job 共用版本、tag commit 與 `master` ancestry 檢查，並將固定 commit 傳給後續工作。只有完整遠端附件驗證通過，且同 workflow、tag、commit 已有成功的正式 run 時，才輸出 `needs_build=false` 省略重複建置。一般 branch／PR／手動 run 永遠正常建置。每個 matrix job 使用獨立的 `windows-2025-vs2026` runner，從 checkout 內取得依賴，不使用編譯 cache。runner 標籤固定工具鏈家族，不保證每次 MSVC／SDK patch version 不變；實際版本必須留在建置紀錄。

```text
正式雙入口：取得同 tag 的 workflow 鎖
  → Prepare event：版本／出典／遠端三附件／先前正式 CI
      ├─ 完整且已有成功同版 CI → needs_build=false
      │    → CI validation → 成功；不再 build／package／publish
      └─ 未完成，或一般 CI → needs_build=true
           ├─ VS2026 Debug   → build / 3 CTest suites ─┐
           ├─ VS2026 Release → build / 3 CTest suites ─┤
           ├─ Ninja Debug    → build / 3 CTest suites ─┼─ 全部成功
           └─ Ninja Release  → build / 3 CTest suites ─┘
                → VS2026 Release → Package.ps1
                     → 玩家 ZIP / SHA-256 / build-info artifact
                     → CI validation
                     → 僅正式版 publish：建立／安全補缺，不覆寫
```

`Prepare event` 執行 package 支援、事件／tag 政策和 fake-backend 發布測試，不產生遠端寫入。四組 build 都保留 `/W4 /WX` 並執行 `Object_Connect.Core`、`Object_Connect.FontSystemLifecycle`、`Object_Connect.GameAudio`，以及 package 支援腳本測試。Ninja 驗證覆蓋 CLion presets 的建置方式，不另外發布第二份遊戲 ZIP。準備 job 失敗時不啟動 matrix；只要任一組失敗，就不能進入正式打包／發版。固定名稱的 `CI validation` job 以 `always()` 匯總 prepare／build／package 結果，不能因上游失敗導致下游 skipped 而誤報成功；publish 也以其成功為前提。只有正式版已完成驗證的去重分支可接受 build／package skipped。

入口驗證前即建立診斷日誌，與建置日誌分開保存，見 16.3.4。PDB 分開放在符號 artifact，不與玩家資源混用。玩家 ZIP、符號與日誌 artifacts 保存 30 天；job 間轉交 VS Release 產物的中間 `release-input` artifact 僅保存 1 天，不作配布。正式版本另由 GitHub Release 附件提供。去重 run 只保留準備驗證紀錄，不另建玩家包或 build 日誌；玩家仍下載既存 Release 的成品。

所有 `upload-artifact` 設定 `overwrite: true`，只允許同一 Actions run 重跑 job 時替換同名 CI artifact，避免 API 409 名稱衝突。此生命週期與 GitHub Release 附件不同：Release 的三個管理附件仍不可覆寫，只能驗證或安全補缺，不能把 artifact 的替換選項延伸到正式附件。

`Package.ps1` 是本機與 CI 共用的產物檢查邊界，而非第二套 build 系統。它處理已建置的 Release 目錄，不代替編譯或測試；檢查 EXE、來源／部署資源集合與 import，再建立 ZIP。`-Version` 可指定安全的版本標籤，省略時使用 EXE 連結時記錄的 commit SHA 前 12 位，而不是打包當下 HEAD；`-InputDirectory` 預設 `target/Release`，`-OutputDirectory` 預設 `target/packages`，既存同名輸出不覆寫。一般本機開發包會記錄並警告 source dirty／link 與目前資源來源不同；正式版本與 CI 則拒絕不一致，正式版本另要求已存在且解出同一 commit 的 tag，annotated／lightweight 皆支援。

缺少 Git、由 source ZIP 取得而沒有 `.git`、尚無首次 commit，或只找到不屬於專案根目錄的父 repository 時，普通 build 以警告並記錄來源／dirty 為 unknown 繼續，不冒用父 repository 的 SHA；可追溯打包則必須具備本專案有效的 Git checkout 與 link-time metadata，未知來源由 `Package.ps1` 明確拒絕，CI 的嚴格驗證不變。

Import 檢查拒絕 Debug CRT、動態 MSVC／OpenMP runtime 及 `dxcompiler.dll`／`dxil.dll`，保留正常 Windows 系統 DLL（例如 `D3DCOMPILER_47.dll`）。ZIP 只包含 `Object_Connect.exe`、完整 `Resources/`、`LICENSES/` 與 `build-info.json`，不包含測試 EXE、PDB 或第三方 library。ZIP 外另提供 `.zip.sha256` 與 `.build-info.json`，便於在下載後核對。

### 16.3 版本來源、權限與不可變性

#### 16.3.1 發佈流程：程式整合、版本標記與雙入口

兩條正式流程共用同一套驗證與發布邊界：維護者可推送 tag，由 CI 建立 Release；也可直接在 GitHub 網頁公開 Release，由 `release: published` 觸發 CI 並補上附件。**網頁流程不需要命令列，也不要求把 GitHub 產生的 lightweight tag 改成 annotated。** Release 頁面已公開不等於遊戲包已通過 CI，團隊須等 publish 完成與三個附件齊全才配布。

| 事件 | 建置與玩家包 | 正式 guard | Release 動作 |
| --- | --- | --- | --- |
| 推送 `master`、PR 目標為 `master` | 執行 | 不執行 | 不執行 |
| `workflow_dispatch`／Run workflow | 執行 | 不執行 | 不執行 |
| 本機 commit／建立 tag | 不執行，尚未送到 GitHub | 不執行 | 不執行 |
| 推送小寫 `v*` tag | guard 成功且未完成去重驗證時執行 | 前置準備 job 共用檢查 | 無 Release 時建立；已公開時驗證並補缺 |
| 公開正式 GitHub Release（`published`） | 同上 | 同上，另拒絕 prerelease | 保留現有 Release，驗證並補缺 |
| 保存 draft、僅 `edited` | 該事件不觸發 | 不執行 | 不執行；draft 真正公開時才觸發 |
| 公開 prerelease | guard 拒絕 | 不支援預發版 | 不添附 |

因此手動 CI 成功，只證明該次建置及一般打包通過，**不等於正式 guard 或發版檢查已通過**。大寫 `V1.1.0`、沒有 `v` 的 `1.1.0` 不符合 tag push 入口；即使透過 Release 公開事件啟動，也會被正式格式檢查拒絕。`v*` 只是事件篩選，不代表 `v1.1` 等所有名稱都是合法版本。

標準整合順序為「工作 branch 的 commit／push → PR → 必要 CI 成功 → 合併 `master` → 確認該版 CI → 網頁公開 Release 或推送版本 tag」。`git tag` 只建立本機參照，tag push 才是正式入口；`master` push 只驗證程式，不自動發版。網頁同時建立新 tag 並公開 Release 是 [GitHub 官方支援的操作](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository#creating-a-release)，不要求拆成命令列步驟。操作指令集中在 [README 的リリースフロー](../README.md#リリースフロー)。

雙事件仍可留下兩筆 Actions run，但在整個 workflow 層級使用同 tag 的 `release-workflow-<tag>` concurrency group，`cancel-in-progress: false` 不打斷正在建置的同版流程。後一輪於取得鎖後才檢查完整附件與先前同 workflow／tag／SHA 的正式成功紀錄；兩者俱備才省略 build、package、publish。完整附件但沒有先前成功 CI 時仍必須完整建置，不能把手工填寫的 metadata 視為 CI 證據。第一輪失敗或僅部分附件也不是完成狀態。一般 CI 使用 run ID 隔離，不因同 SHA 的 Release 存在而免測試。publish 層仍保留另一個 `release-<tag>` 鎖，與可能還在執行的舊 workflow 協調附件寫入，不能和 workflow 鎖使用同 key 而自我等待。此設計使用 [GitHub 官方 concurrency](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency)，不保證事件只到一次或編譯產物逐 byte 可重現。

「線上 `master` 不接受未驗證修改」需要 repository 的保護規則，而非事後 Actions。必要設定為要求 PR、要求固定 **`CI validation`** status check、合併前與目標 branch 保持最新、禁止繞過規則（含管理者），並不允許 force push／刪除。準備、任一 matrix 或 package 失敗都必須由此集約 check 回報失敗，不能將可能 skipped 的 package job 單獨當作合併保證。這些是管理者需另外啟用的 [GitHub protected branches](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches) 設定；本次檔案修改沒有設定遠端保護，也不能撤回已受理的直接 push。具體操作與檢查清單以 README 為準。

目前流程於 Release 公開後添加附件，因此不相容於 repository 的 **Immutable releases** 選項；該選項在公開後禁止增加附件。若要啟用，須先另行改為 draft → 上傳全部附件 → 公開，不可誤把本專案「不覆寫既有附件」規則當成已支援 GitHub 的 immutable 發布生命週期，也不自行變更遠端設定。

#### 16.3.2 CI 強制規則與失敗階段

以下是程式實際執行的 gate，不是可選建議。來源為 `.github/workflows/windows-build.yml`、`scripts/ReleasePolicy.ps1`、`scripts/Assert-ReleaseTag.ps1`、`scripts/ReleaseBuildSupport.ps1`、`Package.ps1` 與 `scripts/ReleasePublishSupport.ps1`。

| 檢查階段 | 強制條件 | 不符合時的結果 |
| --- | --- | --- |
| `Prepare event`，在 configure／build 前 | 正式名稱必須是 `vMAJOR.MINOR.PATCH` 三段非負整數，總長度不超過 80 字元；各段只能是 `0` 或不以 `0` 起頭的數字。不接受 prerelease 或 build metadata，Release 也不能標記為 prerelease。 | 準備 job 明確失敗；matrix、package／release 不執行。 |
| 同一 tag guard | annotated／lightweight 均可，但必須直接以 commit 為目標，不支援指向另一個 tag 的 nested tag；解出的 commit 必須等於 checkout `HEAD`。 | 拒絕非 commit 的目標與事件／checkout 不一致的來源，而非因 lightweight 類型拒絕。 |
| 同一 tag guard | tag commit 必須已在 `origin/master` 的歷史中（ancestor 檢查），不要求一定等於目前 branch tip。 | 尚未合併至 `master` 的 commit 不得發版。 |
| 準備階段的去重檢查 | 下載完整三附件，驗證版本／commit／hash／ZIP 內容，且有相同 workflow、tag、SHA 的正式 run 成功紀錄。 | 兩者皆成立才 `needs_build=false`；沒有完整附件或成功紀錄則完整驗證，API 錯誤或附件不自洽則停止。 |
| 四組 `Configure, build and test`（新版本／未完成版） | VS2026／Ninja × Debug／Release 全部 build、三項 CTest suites 與 package 支援測試成功。 | 任一 matrix job 失敗，後續 package／release 不執行。 |
| `Package verified Release` | Package 整合測試成功；輸入為 Release x64，具有效完整 commit 和 link-time metadata；EXE SHA-256 與 metadata 相符。CI 的建置 commit、打包 checkout 必須相同且兩者均為 clean；正式版本的 tag 也必須解出該 commit。 | 不建立可發布的玩家包，release 不執行。未知來源、髒工作目錄及混用舊 EXE 都會被拒絕。 |
| 同一 package 階段 | 原始與部署資源的檔案集合、逐檔 SHA-256 相符，必要字體／CSV 和授權資料齊全；通過 DLL import／sidecar／開發二進位檢查，且輸出檔案不存在。 | 拒絕缺檔、資源被改動、不合規依賴或覆寫已有 ZIP；具體邊界見 16.2。 |
| publish 的遠端重驗 | tag 仍解出本次已測試的 commit；若已有 Release，必須為相同 tag 的公開正式 Release。Release 事件另綁定原始 Release ID。 | tag 被移動／替換、既有 Release 是 draft／prerelease、事件 Release 消失或被刪除重建時停止，不自動重建或改狀態。 |
| 同一 publish 階段 | ZIP、校驗檔及外部 build-info 三個候選附件皆存在；ZIP SHA-256 正確，build-info 的 version／built commit 與事件相同且建置來源為 clean。 | 不發佈不一致的包；API／權限／建立 Release 出錯也會明確失敗。 |
| 完整既有管理附件 | 下載遠端 ZIP、`.zip.sha256`、`.build-info.json`，驗證相互 hash、version、commit 正確。 | 正確則 no-op 成功；不要求與重新 build 的候選逐 byte 相同。不自洽則停止。 |
| 部分既有管理附件 | 現有的每個管理附件均須與本次候選逐 byte 相同，才上傳缺少的檔案。 | 任何同名衝突都停止；不使用 clobber、不刪除、不覆寫。新舊 build 的時間／工具鏈差異可能造成安全停止。 |
| `CI validation` 匯總 | prepare 成功，且所需 build／package 全成功；或正式版已完成去重驗證、兩者依預期 skipped。 | 上游失敗、取消、非去重原因的 skipped 都回報失敗；publish 必須等待此 check 成功。 |

名稱範例：`v1.1.0`、`v0.0.1` 合法；`v1.1`、`v01.1.0`、`v1.01.0`、`v1.1.00`、`v1.1.0-rc.1`、`v1.1.0+build.1` 都會在正式 guard 被拒絕。lightweight tag 與已存在的公開 Release 本身不再是錯誤；其他 gate 不因網頁入口而放寬。

管理附件僅為該版本的三個固定檔名。既存 Release 的標題、內文和其他手動附件全部保留；新 Release 才自動產生 notes。若不存在 Release，由 tag 流程建立；若已存在，依上述完整／部分規則驗證或補齊。即使只有空的公開 Release，也可添附本次完整候選包。

#### 16.3.3 團隊必遵守的操作約定與推薦規範

以下操作要求由維護者遵守，**目前沒有全部自動強制**，不得與上表的 CI gate 混為一談：

- 發版前先以 PR 將變更整合至 `master`，確認該 commit 的通常 CI 成功。新版本正式流程會重新測試，但不查詢先前的 `master` CI 結果；去重分支查的是同版本正式流程的成功。命令列操作時另須同步本機並確認工作目錄乾淨；CI 的 clean checkout 檢查不能證明操作者本機當時沒有未提交檔案。
- 網頁操作選定 `master` 作為新 tag 的 target；命令列推薦 `git tag -a` 保留註記，但不是 CI 強制。只推送指定 tag，不使用 `git push --tags`；workflow 不檢查一次推送了幾個 tag。
- 已發佈 tag／附件視為不可變，不得刪除、force push、移動或手動覆蓋。CI 會比對當下遠端 tag、只安全補缺，但不等於 repository 已設定禁止維護者修改 tag 的保護規則。
- MAJOR 表示不相容變更、MINOR 表示新功能、PATCH 表示修正；版本高低與變更內容由維護者判定，CI 不自動升版，也不檢查新版本是否大於所有既有版本。

新 commit 的 `<type>(<scope>): <description>` 形式、小寫英文 type／scope 及日語描述是**推薦規範，不是 CI 強制規則**。目前沒有 commit lint，也不因舊有 `update:`／`hotfix:` 格式拒絕建置或改寫 Git 歷史。操作指令、commit 範例及發版檢查清單以 [README](../README.md) 為單一來源。

追溯關係固定為：

```text
annotated / lightweight tag（正式版）或短 SHA（一般 CI）
  → 完整 source commit + 同 commit 的 SDK 快照
  → EXE 連結時的 build metadata + 實際工具鏈版本
  → BloodLine-windows-x64-<version>.zip + build-info.json
  → ZIP SHA-256 → Actions run / GitHub Release 附件
```

一般 job 使用 `contents: read`，prepare 另有 `actions: read` 查詢成功 run，遠端去重僅有讀權限。PR 採 `pull_request` 而非特權 `pull_request_target`。官方 Actions 固定完整 commit SHA，更新需顯式修改 workflow；僅正式 tag push／Release 公開的 publish job 取得 `contents: write`。該 job 只 checkout 已通過四組驗證、且屬於 `master` 歷史的固定 commit 之 `scripts`，不執行遊戲；checkout 不保存憑證，`GH_TOKEN` 僅注入必要的遠端檢查／公開 step，準備階段使用唯讀 token。不新增私人依賴下載 secrets，也不讓建置本身自動 commit、push 或建立版本 tag。

#### 16.3.4 診斷與重試界線

應開啟第一個失敗 job 的 step 原文，而不是只看頁面底部 Annotations。現在版本／commit 驗證在前置準備 job 執行；若此處失敗，四組 build 會跳過，不能把它判定為 Release 編譯器問題。

前置 job 在入口驗證前建立 transcript／診斷目錄，以 `always()` 保存 `logs-prepare-<sha>`。建置 job 另保存 CMake／CTest／build 日誌，入口失敗不再依賴尚未開始的 `Invoke-CiBuild.ps1` 建立日誌。checkout 或日誌系統本身先失敗仍可能沒有 artifact，應讀 Actions 原始 step；不能以缺少附件警告替代真正錯誤。

同 tag 有兩筆 run 時先區分事件與進度；等待同版 workflow 鎖，以及第二筆通過準備驗證後的 build／package／publish skipped 都是正常去重結果。不能用 Actions 列表筆數判斷有沒有重複編譯。

暫時性 runner／網路問題可在 tag 與 source 都未變時重跑正式 run，不因已有公開 Release 而拒絕；完整附件及先前正式 CI 驗證後可在編譯前 no-op，部分附件仍須與候選一致才能補齊。若候選因重建而與部分附件不一致，安全停止並用新版本發版，不能自動清除既存附件。需要修改 source 或修正非法版本時也用新 commit／版本，不移動已發佈 tag。

只有 publish 失敗時，優先 `Re-run failed jobs` 重用原始 `player-package`，不要先重跑全部 job 而產生可能不同的 ZIP bytes。artifact 已過期／缺失時，須以新版本發版或重新通過完整 build、test、package；完整重跑仍受既有部分附件的逐 byte 一致限制，衝突就用新版本，不放寬內容檢查。

本次規則只隨包含新 workflow／腳本的 commit 生效。先將修改整合至 `master` 並通過通常 CI，再從該版本建立新 Release 或 tag。GitHub 對舊 run 的重跑仍使用舊 workflow 與來源；不會因 `master` 後來更新而移除舊的 annotated-only／既有 Release 禁止條件，也不會自動取得新的編譯前去重。遷移不刪除或重指歷史 tag，操作步驟集中於 README。

### 16.4 驗收邊界

依賴封裝的回歸驗證包含：外部引擎環境變數不存在、舊 cache 指向失效路徑、全新建置目錄，以及 SDK／資源缺檔時的明確錯誤。打包驗證包含禁止 DLL import、ZIP 內容白名單、來源與部署資源一致、SHA-256 和 link-time metadata。文件中的參數、命名、觸發條件必須與實際腳本和 workflow 一致。

本機 build／CTest 成功不等於 GitHub Actions 已通過；首次 push 後仍須確認真實雲端 run。Hosted runner 裝有開發工具，headless 測試與 import 檢查不能證明乾淨使用者環境、GPU 或音訊一定正常。發版前仍須在沒有開發環境的 Windows 機器解壓完整 ZIP，驗證啟動、畫面、日文字體、音效和基本關卡流程，這項結果須與 CI 結果分別記錄。
