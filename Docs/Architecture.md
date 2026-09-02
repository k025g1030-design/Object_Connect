# Object_Connect 架構

## 設計邊界

本作是固定 1280×720 的 2D Game Jam 解謎。架構只分離資料、規則、血管模擬與
繪製，不建立大型遊戲框架。公開 API 固定在 `include/ObjectConnect/`，實作放在
對稱的 `src/ObjectConnect/`；所有程式使用 `object_connect` namespace。

```text
Object_Connect executable
  -> object_connect_runtime
       Application / Input / Game / UI / PuzzleRenderer
       KamataEngine / DirectX 12
       -> object_connect_core
            Math / JSON Data / Tile Geometry
            GameFlow / PuzzleBoard
            BloodTentacle / RibbonStrip / TileMesh

Object_Connect_CoreTests -> object_connect_core
```

core 不 include KamataEngine、Win32 或 D3D 型別。Renderer 只讀 definition 與
snapshot，不回寫 gameplay。兩個 renderer 使用 PIMPL 隔離 GPU ownership。

## 模組 ownership

| 模組 | 擁有 | 不擁有 |
| --- | --- | --- |
| Data | schema v1 JSON、path resolution、cross-file validation、normalized catalog | drag state、D3D |
| Geometry | world/cell conversion、stamp hit、segment-solid-tile query | collision response |
| PuzzleBoard | activated graph、capacities、preview、segments、全域長度、solved | 檔案、UI、分數 |
| BloodTentacle | 一個 segment 的 Verlet 粒子與 tip mode | graph、關卡預算 |
| RibbonStrip / TileMesh | 純 CPU 頂點建立 | GPU buffer、gameplay |
| GameFlow | menu screen 與 command | board simulation |
| Rendering | atlas batch、flat pixel ribbon、layer order、HUD | 規則修改 |
| Game | catalog/session/flow/input/renderer 的唯一組裝點 | 低層算法 |

「一條 gameplay 血管」和「多個 simulation instances」不是同一層概念。每個
committed edge 保留自己的 Verlet chain，方便固定兩端與下垂；所有 chain 仍由
同一個 board、同一筆 `totalLength` 和同一張已提交 graph 管理。

## 資料載入

`catalog.json` 是唯一入口，參照同目錄下的 tileset、node types 與 levels。相對
路徑在 loader 內正規化；每份 JSON 與 atlas 都必須是 canonical resource root
內的 regular file，因此 absolute path、`..`、symlink／junction 越界都會被拒絕。

```text
read every referenced file
  -> parse into private nlohmann::json DOM
  -> exact key/type/range validation
  -> cross-file ID/tile/type validation
  -> resolve node stamps, anchors, edge indices
  -> overlap + solid LOS validation
  -> DAG + multi-root reachability validation
  -> build temporary PuzzleCatalog
  -> move into caller output only after every level succeeds
```

JSON DOM 不出現在 header。錯誤格式是 `filename + JSON pointer + detail`；caller
傳入的既有 catalog 在任何失敗下保持不變。

Normalized data 主要分成：

- `TilesetDefinition`：atlas path/dimensions 與 tile cell lookup。
- `NodeTypeDefinition`：display name、rectangular `TileStamp`、occupied mask、anchor。
- `PuzzleDefinition`：兩個固定 `TileGrid`、resolved node instances、candidate edges、
  root/goal indices與規則。
- `PuzzleCatalog`：一份 tileset、一份 node type catalog、多個 levels。

tile ID `0` 不會出現在 tileset definitions，永遠代表空白。Node stamp 的 occupied
mask 從非零 tile 解析一次，Board hit-test 與 renderer 使用同一份結果。

候選 edge 允許 branch/merge，但必須是 DAG。所有節點要從至少一個 root 可達；
不要求唯一 terminal。Loader 不求解「容量 + 長度是否能同時激活所有 goal」，
避免把完整 solver 塞進 Jam runtime。

## PuzzleBoard

Board 初始化時激活全部 root，建立每個 node 的 incoming/outgoing count，以及每個
edge 的 committed bit。一次只有一個 preview，但可從任何 available source 開始。

```text
Idle
  left press occupied tile of an available active source
    -> Dragging(source, selected authored edge)

Dragging
  move over another authorized target -> preview follows target candidate
  release valid target               -> Commit
  release invalid / focus cancel     -> Retracting

Commit
  consume source outgoing + target incoming
  mark edge committed
  activate target (source stays active)
  attach segment tip to target anchor
  if every goal active -> Solved

Retracting -- ~0.22 s --> remove preview and refund reservation
```

可用 edge 必須同時滿足：未提交、source 已激活、source outgoing 未滿、target incoming
未滿。commit 額外要求 pointer 命中 target occupied mask、reservation 足夠，且
anchor-to-anchor segment 沒有碰到 expanded solid tile。

永遠維持：

```text
committedLength + reservedLength + remainingLength = totalLength
```

一般拖曳使用
`reservedLength = max(previousReserved, distance(root,cursor) * slackRatio)`；hover
授權 target 的任一 occupied cell 時，再與該 edge 的 anchor-to-anchor 最短需求取
最大值，最後 clamp 到本次可用剩餘量。取消 preview 不改 committed state。Retry
直接建立新 board。

Snapshot 公開 activated node indices、available source indices、selected source、各
node incoming/outgoing count、tentacle render data，以及分開的 `lengthExhausted`／
`routeBlocked` flags。計分只讀 activated nodes/committed edges；Board 不知道器官
語意。

## Tile geometry

畫布 cell 是 half-open：左／上邊包含，右／下邊不包含。Node hit-test 先把 world
point 轉成相對 stamp cell，再查 occupied mask；透明 hole 不可點擊。

solid query 逐一測試 obstacle layer 的非零 cell：

```text
expanded padding = segment max half width + pixel padding
segment vs expanded cell AABB
```

接觸邊界算阻擋。這只是 LOS；Verlet 粒子不做 tile collision。關卡若需繞過骨骼，
必須用 anchors/中繼 node 把路徑拆成多條可見線段。

## BloodTentacle 與 ribbon

每粒子保存 `position`／`previousPosition`。每幀最多八個 1/120 秒 fixed substeps；
Verlet integration 後做六輪相鄰距離約束，並重 pin root/tip。Following tip 使用短
target history 形成拖尾；Attached tip 固定到任意 anchor。Paused 不呼叫 update。

`BuildRibbonStrip` 為每個中心點輸出 left/right 頂點。內建 style 使用 16px 等寬
深紅輪廓、穩定的 inward variation 與 2px grid snapping；renderer 再疊 dark edge、
crimson core 與 deterministic flesh flecks。沒有逐幀亂數或 sprite-sheet。

## Rendering

Renderer 有兩條 pipeline：

- textured tile pipeline：`POSITION + TEXCOORD + COLOR`、單 atlas、alpha blend、
  point/nearest clamp sampler。
- flat pipeline：hint、pulse、triangle-strip vessel 與 flesh pixels。

CPU `TileMesh` 將非零 tile 轉為六個 triangle-list vertices，UV 由 tile ID 對 atlas
cell lookup；背景、obstacle、node stamp 共用 builder 與 atlas。

固定 command order：

```text
background color + background tiles
-> optional edge hints
-> vessel outline / core / flesh pixels
-> solid obstacle tiles
-> available-source pulse + node stamps (inactive nodes dimmed)
-> ASCII HUD / menu overlay
```

Obstacle 在血管後面繪製，以遮住純視覺的粒子穿越。這不取代 commit LOS。

## Game session

`Game::Impl` 擁有唯讀 catalog、flow、optional current puzzle index、optional board、
input 與 renderer。Start/Retry 建立全新 board；返回選關/主選單銷毀 board。
LevelSelect 依目前的全域選取 index 自動切換每頁最多七項；GameFlow 本身仍只處理
一份連續的資料驅動清單，最後一項固定是 Back。

初始化順序：JSON catalog → input → atlas/renderer pipelines → UI。Solved 後保留 board
畫面，0.6 秒後才開放完成選單。失焦先立即取消 preview，再切 Paused。

## 測試與修改指南

Headless suites 分為：JSON transaction/schema、tile geometry、multi-root board/global
length、BloodTentacle、RibbonStrip/TileMesh、GameFlow。Debug/Release 都使用 `/W4 /WX`
並跑 CTest。GPU layer order、nearest pixel clarity、pulse、拖曳手感與失焦仍需人工測試。

修改時先找 ownership：

- 格式/合法性：Data loader。
- 激活、容量、長度、commit：PuzzleBoard。
- 粒子手感：BloodTentacle。
- 中心線外觀：RibbonStrip。
- atlas UV / draw order：TileMesh / PuzzleRenderer。
- screen/menu：GameFlow / UI。

不要讓 renderer 修改 board，也不要讓 BloodTentacle 讀 node ID 或 JSON。

## 非目標

不包含 editor、Tiled TMJ/TXT importer、自動 solver、存檔、熱重載、動畫 effect
system、粒子對 tile 的物理碰撞、逐段 undo、creature controller、ECS、完整物理
引擎或可調解析度。
