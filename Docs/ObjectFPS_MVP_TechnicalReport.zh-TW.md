# Object_FPS 完整 MVP 技術設計與實作報告

- 文件狀態：完整 MVP 實作後的技術、診斷與決策紀錄
- 日期：2026-08-28
- 日本語版：[ObjectFPS_MVP_TechnicalReport.ja.md](ObjectFPS_MVP_TechnicalReport.ja.md)
- KamataEngine ABI 專題：[EnemyAtlasShaderABI.zh-TW.md](EnemyAtlasShaderABI.zh-TW.md)
- 架構參考：[Architecture.md](Architecture.md)

## 1. 報告目的與結論

本報告記錄 Object_FPS 從可玩 MVP 擴充至素材化版本時的完整工程判斷，範圍包括資料驅動敵人、
Atlas 動畫、事件幀、事件與快照邊界、死亡生命週期、天空球、場景亮度／Gamma、Direct3D 12
離屏渲染，以及整合 KamataEngine 時暴露的 constant-buffer ABI bug。

最終設計可以濃縮成五個原則：

1. **靜態差異由 CSV 管理，通用行為規則由程式管理。** 敵人的尺寸、素材、clip、速度與事件幀是資料；
   state loop／clamp、事件跨越判定與傷害流程是程式規則。
2. **玩法形狀與顯示畫布分離。** Hitbox 不再由貼圖尺寸決定，billboard 也不再由 hitbox 高度縮放。
3. **Simulation 透過短生命週期事件輸出行為，透過快照輸出狀態。** Renderer 不擁有 AI 或傷害狀態。
4. **世界 3D 與畫面 overlay 分開調色。** Sky／Map／Enemy／Projectile 先進離屏場景；HUD／UI／Fade
   在 composite 後繪製，保持原色。
5. **對引擎 ABI 採可驗證的局部相容層。** 目前由專案 HLSL 配合預編譯 KamataEngine 的實際 CPU layout，
   並以 `static_assert` 與測試阻止靜默回歸。

這次敵人狀態顯示錯列不是 CSV 或 state machine 的根因，而是 KamataEngine 原本就存在、過去被零 UV
offset 掩蓋的 CPU／GPU layout 不一致。Enemy Atlas 首次大量使用非零 X／Y offset，才讓 bug 顯現。

## 2. 需求、限制與最初問題

### 2.1 功能需求

- 地圖仍可編輯或擴建，玩家不能走出天空背景。
- 使用原始 `sky_sphere.png`，不為 MVP 建立新的天空資產管線。
- 原始世界素材偏暗，需要全場景一致的亮度調整，但武器、HUD、UI、文字與 Fade 必須維持原色。
- 每種敵人使用一張包含 `idle / move / attack / dead` 的 Atlas。
- 敵人 hitbox、billboard 顯示尺寸、frame pixel size、clip 與攻擊事件必須資料化。
- 動畫 frame 有大量透明留白；不同動作的可見 alpha 範圍並不等於物理碰撞範圍。
- 攻擊傷害或發射必須與動畫事件幀同步，而不是一進入 attack state 就立即發生。

### 2.2 工程限制

- Object_FPS 使用預編譯 KamataEngine library；不能只改外部 header 就改變 library 的 constant-buffer 寫入。
- Gameplay／Data 需維持與 KamataEngine、Win32、D3D12 無關，才能做 headless contract tests。
- 現階段視窗禁止 resize；離屏資源可在初始化時依 backbuffer 尺寸建立一次。
- 現有素材為 equirectangular sky PNG 與單一 enemy sheet，不導入 cubemap、texture array 或完整資產烘焙工具。
- MVP 以少量敵人為目標，不先導入 ECS、通用事件匯流排或高階 render graph。

### 2.3 開始時容易混在一起的三種問題

敵人「看起來尺寸錯、動作錯、狀態也錯」其實包含三個不同層次：

- **透明留白問題**：固定 frame canvas 中可見人物只占一部分，會讓肉眼感到偏小或偏位。
- **玩法／顯示耦合問題**：若用 hitbox height 畫 billboard，或用 alpha bounds 當 hitbox，兩邊都會失真。
- **Atlas row 取樣問題**：CPU 已選到正確 move clip，但 GPU 仍可能從 dead row 取樣。

只有先把這三者分開，才不會用修改 CSV 尺寸掩蓋 shader ABI 錯誤。

## 3. 整體分層與責任邊界

```text
CSV files
  -> GameDataLoader / immutable definitions
      -> EnemySystem / EnemySpawnDirector
          -> per-frame EnemyAttackEvent
          -> read-only EnemySnapshot
              -> Game orchestration
              -> EnemyBillboardRenderer definition cache

Game::Draw
  -> offscreen world scene
      Sky -> Map -> Enemy -> Projectile
  -> ScenePost composite
  -> Weapon/HUD -> UI -> Fade
```

| 層 | 擁有內容 | 不應擁有 |
| --- | --- | --- |
| Data | CSV parse、definition、外鍵與資源路徑驗證 | AI runtime、GPU resource |
| Gameplay/Enemy | state、導航、HP、cooldown、event、snapshot | Player HP owner、projectile simulation、texture |
| Game | subsystem 更新順序、事件消費、傷害與 projectile 組裝 | Atlas UV 或 D3D12 descriptor |
| Rendering | texture／material cache、billboard、sky、post-process | AI transition、damage、spawn quota |
| Core | KamataEngine lifetime、frame 邊界、視窗模式 | 關卡或渲染內容 |

`Game` 是 composition root。這讓 EnemySystem 可以只輸出「發生了一次近戰／遠程攻擊」而不直接 include
`PlayerCombatState` 或 `ProjectileSystem`；Renderer 也只能讀 snapshot，不能回寫 simulation。

## 4. 診斷與思考路徑

### 4.1 先定義不可變條件

需求不是「畫一顆球」或「把圖片變亮」，而是：

- 天空永遠是背景，不是地圖邊界。
- Hitbox 表示玩法身體，render canvas 表示素材畫布。
- 同一個 attack 的傷害最多發生一次，而且要和指定 frame 同步。
- UI 顏色不能受到世界亮度調整。
- CPU 設定的 Atlas X／Y offset，GPU 必須讀到相同值。

先建立這些 invariant，才能比較方案而不是只讓單一測試畫面看起來正常。

### 4.2 敵人錯誤的排查順序

1. 由「仍存活且移動中的敵人顯示 dead pose」建立最小矛盾。
2. 核對 CSV 的 state、origin、frame count 與 `EnemyState -> clip` switch。
3. 核對 runtime snapshot 的 state 與 elapsed，確認 AI 沒有誤進 Dead。
4. 檢查 PNG dimensions、frame rectangle 與透明 alpha 範圍。
5. 確認透明留白能解釋可見大小，但不能解釋整個 move row 變成 dead row。
6. 單獨驗證 CPU half-texel、frame X、state Y 與 V 軸 UV 計算。
7. 沿 `Material::uvOffset_ -> constant buffer -> Obj.hlsli -> ObjPS` 比較 byte layout。
8. 從錯位 byte mapping 預測畫面：U 取得 intended Y，V 變成零；預測與實際症狀一致。
9. 只修 ABI，不調 CSV／state machine；若所有 state 同時恢復，即可反證根因。

核心判斷是：**alpha 留白可以讓人物顯得小，卻無法把素材替換成另一列；只有 row offset 遺失或錯讀能同時
解釋狀態列與水平 frame 都異常。**

### 4.3 亮度問題的拆解

素材偏暗也不是單一參數問題：

1. 共用 OBJ shader 的方向光可能再次壓暗本來就已著色的 2D 素材。
2. 即使去除 diffuse/specular，整個世界仍需要可統一調整的 tone control。
3. 若直接處理 backbuffer，HUD／UI 也會變色。

因此採兩層方案：先把 world material 設為白色 ambient-only／unlit-like 基底，再只對離屏 world scene
做 Brightness／Gamma composite。

### 4.4 以可證偽測試收斂

- 故意令 test definition 的 `hitboxHeight=3.5`、`renderHeight=0.8`，驗證 renderer 沒有偷用 hitbox。
- 以大 `deltaSeconds` 一次跨過 event time，驗證事件不會漏掉或重複。
- 同 frame 先排入 enemy attack，再擊殺敵人，驗證 queued event 被取消。
- 驗證 Atlas 首格、尾格、V 翻轉、half-texel 與 out-of-bounds。
- 驗證 Gamma 2.2 的 exponent 為 1、black 保持 black、Brightness 提高中間調。

## 5. 資料驅動契約

### 5.1 為何使用兩份敵人 CSV

`enemies.csv` 是「每種敵人一列」的共用 definition；`enemy_animation_clips.csv` 是
「每種敵人對多個 state」的一對多資料。若把四個 state 全部橫向塞入 enemies row，欄位會快速膨脹，
也很難以 `(enemy_id, state)` 驗證重複與缺漏。

`enemies.csv` 保存：

```text
enemy_id, kind, damage, attack_interval_seconds, hp, defense,
hitbox_radius, hitbox_height, render_width, render_height,
texture_name, frame_width_px, frame_height_px
```

`enemy_animation_clips.csv` 保存：

```text
enemy_id, state, origin_x_px, origin_y_px, frame_count,
seconds_per_frame, event_frame_index, muzzle_x_px, muzzle_y_px
```

### 5.2 資料與程式的分界

| 放在 CSV | 留在程式 |
| --- | --- |
| 敵人數值、hitbox、render canvas、sheet、frame size | `EnemyState` 的有限集合 |
| 每個 clip 的 origin、frame count、SPF | idle／move loop；attack／dead clamp |
| attack 的 event frame | 跨越 event time 的一次性判定 |
| ranged muzzle pixel | 世界座標轉換、LOS、傷害／projectile 規則 |

這避免把 CSV 變成難以驗證的腳本語言，同時讓素材作者不必為每個敵人修改 C++。

### 5.3 載入與交叉驗證

Loader 嚴格驗證：

- 精確 header、UTF-8 BOM／LF／CRLF、quoted field 與 doubled quote。
- ID、kind、數值範圍、duplicate ID、相對資源路徑與 `..` 防護。
- Clip 只能是 `idle / move / attack / dead`；每個敵人四個 state 必須恰好各一列。
- `frame_count > 0`、`seconds_per_frame > 0` 且必須有限。
- `event_frame_index` 為 0-based，只允許 attack，且必須 `< frame_count`。
- Ranged attack 的 muzzle X／Y 必須成對存在並位於單格內；其他列禁止 muzzle。
- Rectangle 算術先升為 `uint64_t`，拒絕 64-bit 與 32-bit pixel coordinate overflow。
- Clip 的 `enemy_id` 必須引用已存在的 enemy definition。

CSV 層無法知道 GPU 最後載入的實際 PNG dimensions，因此 Renderer 載入 texture 後，再用
`D3D12_RESOURCE_DESC` 驗證 33 個 frame rectangle 都位於實際 sheet 內。這是「語意驗證」與
「實體資源驗證」的兩層防線。

目前 loader 仍限制恰好一個 melee 與一個 ranged definition。若未來同 kind 需要多個 archetype，
必須先擴充 selection／spawn policy，不能只往 CSV 加列。

## 6. Definition、Runtime、Snapshot 與 Event

四種資料的生命週期不同：

| 類型 | 生命週期 | 內容與用途 |
| --- | --- | --- |
| `EnemyDefinition` | Catalog／關卡期間 | 靜態 combat、hitbox、render、sheet、clips |
| `RuntimeEnemy` | 單一敵人從 spawn 到 retire | state、HP、elapsed、cooldown、path、event flag，並凍結 spawn 時 definition |
| `EnemySnapshot` | 更新後的只讀投影 | ID、definition ID、state、position、hitbox、HP、flash、elapsed |
| `EnemyAttackEvent` | 僅當前 update 到下次 update 前 | origin、target、damage 與 attack identity |

### 6.1 快照的決定

Snapshot 不複製 texture path、frame pixels 或 clips。Renderer 以 `definitionId` 查詢初始化時建立的 definition／
texture／material cache；simulation 的 state 與 elapsed 只決定目前應顯示哪一格。

這個邊界有三個好處：

- Gameplay 不依賴 KamataEngine 或 GPU resource。
- Renderer 無法直接改變 HP、AI 或 cooldown。
- 每 frame 不必為每個敵人複製大量素材資料。

精確地說，Snapshot 並非「只有 definition ID」；它仍保留 hitscan、HUD／debug 或 renderer 需要的 runtime scalar。
此外目前 `RuntimeEnemy` 仍保存完整 definition copy，以凍結 spawn 當下資料。MVP 數量下可接受；大量敵人時可改為
catalog-owned immutable handle，但必須先保證 catalog lifetime 與 hot reload 語意。

`GetSnapshots()` 與 `GetAttackEvents()` 回傳 `std::span`；consumer 不可跨越下一次 system mutation 保存該 view。

### 6.2 事件不是通用非同步匯流排

目前的 event-driven 指「system 每 frame 產生短生命週期 output queue」，不是跨 thread 或可持久化的 message bus。
`EnemySystem::Update` 開頭會清除上一 frame 的 attack events，所以 Game 必須在下一次 Update 前消費。
Weapon 也採相同模式：`WeaponController` 產生 `ShotEvent`，由 Game 解決 hitscan、傷害與 tracer。

## 7. Hitbox 與 Render Canvas 分離

目前資料為：

| Enemy | Hitbox radius | Hitbox height | Render width | Render height | Frame pixels |
| --- | ---: | ---: | ---: | ---: | ---: |
| `melee_basic` | 0.20m | 0.80m | 0.973913m | 0.80m | 560×460 |
| `ranged_basic` | 0.20m | 1.60m | 1.230769m | 1.60m | 700×910 |

- Spawn safety、navigation clearance、surface distance、live collider 與 hitscan capsule 使用 hitbox。
- Billboard scale 只使用 `render_width / render_height`。
- Billboard center Y 固定為 `render_height / 2`，讓完整 canvas 的底邊落在地面。

Render size 的語意是「包含透明留白的固定 frame canvas 世界尺寸」，不是人物可見 alpha bounding box。
所有動作維持同一 canvas、pivot 與地面 anchor，可避免每格自動 trim 後造成縮放、腳底與槍口跳動。

代價是透明留白很大的 frame 會讓可見人物偏小。若未來需要裁切，應在 CSV 新增 per-clip trim rect、pivot／
ground anchor 或 visible scale；不能每 frame 即時計算 alpha bounds，否則動畫會抖動。

## 8. Atlas 動畫與 Billboard 技術

### 8.1 State 與 frame 規則

```text
Idle      -> animations.idle      -> loop
Moving    -> animations.moving    -> loop
Attacking -> animations.attacking -> play once, clamp final frame
Dead      -> animations.dead      -> play once, clamp final frame
```

Frame index：

```text
rawFrame = floor(stateElapsedSeconds / secondsPerFrame)
looping   = rawFrame % frameCount
one-shot  = min(rawFrame, frameCount - 1)
```

Attack 與 Dead 停在尾格，避免 simulation 等待 retire／state transition 時突然回到第一格。

### 8.2 UV、V 軸與 half-texel

每格的 rectangle 明確由下列座標得到：

```text
left   = originXpx + frameWidthPixels * frameIndex
top    = originYpx
bottom = top + frameHeightPixels
```

為避免線性取樣跨到鄰格，UV 端點落在 texel center：

```text
offsetX = (left + 0.5) / sheetWidth
offsetY = (bottom - 0.5) / sheetHeight
scaleX  = (frameWidth - 1) / sheetWidth
scaleY  = -(frameHeight - 1) / sheetHeight
```

共用 `map_wall.obj` 經 KamataEngine OBJ loader 後，quad 頂部的 V 方向與 Atlas 左上角座標不同，因此從
frame bottom pixel center 開始並使用負 `scaleY`，讓素材保持正立。

Billboard yaw 由 enemy 到 viewer 計算。共用 quad 的正面會使 U 在畫面鏡像，因此 yaw 額外加 π，並以
culling disabled 從正確一面呈現。這是素材朝向修正，不是 AI facing state。

### 8.3 Cache 與 draw

- 共用一個 quad model。
- 每個 definition ID cache 一張 sheet texture；目前兩個 definitions 合計兩張。
- 為 17 個 melee frames 與 16 個 ranged frames 建立 33 個獨立 frame materials；初始化後不再改寫其 UV constant buffer。
- 每格 material 保存對應 UV scale／offset；Draw 時依 snapshot state＋elapsed 選 material。
- Instance 只保存 identity、state／elapsed、Object3d、ObjectColor 與 billboard yaw。

此方案避免每 frame 改寫同一 material constant buffer，但 frame／敵人種類大量增加時，material 與 draw call
數會線性增加；那時可改 texture array、instance data 或敵人專用 pipeline。

### 8.4 透明 depth 與 hit flash

僅使用 alpha blending 時，alpha 幾乎為零的 quad pixel 仍可能寫入 depth，遮住稍後繪製的物件。
共用 `ObjPS.hlsl` 因而在取樣後執行：

```hlsl
clip(texcolor.a - 0.01f);
```

這解決透明 Atlas texel 的 depth occlusion，但不是 state／row 錯配的根因。因為它位於共用 OBJ shader，
所有使用該 `ObjPS` 的 OBJ 貼圖中，`texcolor.a < 0.01` 的 texel 都會被丟棄；判斷對象不是 material alpha、
`ObjectColor.a` 或最終輸出 alpha。半透明排序也沒有因此得到解決。

正常 enemy color 為白色；受擊時 `ObjectColor` 暫時提高至約 1.5 倍，持續 0.12 秒。世界材質不再以
melee 紅、ranged 藍 tint 掩蓋原始貼圖。

## 9. 事件幀驅動的攻擊

### 9.1 時間語意

`event_frame_index` 為 0-based：

```text
eventTime = eventFrameIndex * secondsPerFrame
```

| Enemy | Attack frames | SPF | Event index | Event time | Attack duration |
| --- | ---: | ---: | ---: | ---: | ---: |
| melee | 6 | 0.05s | 3 | 0.15s | 0.30s |
| ranged | 5 | 0.05s | 2 | 0.10s | 0.25s |

每次進入 attack 時將 elapsed 歸零、`attackEventEmitted=false`，並從起手時開始計 attack interval cooldown。
Event index 為 0 時立即觸發；其他 event 使用：

```text
previousElapsed <= eventTime && currentElapsed >= eventTime && !emitted
```

這不是等待浮點值「剛好等於」事件時間，因此 frame hitch 或大 delta 一次跨過 0.15 秒仍會發生一次；
`attackEventEmitted` 防止重複。

Attack state duration 直接由 `frameCount * SPF` 計算，且資料驗證要求 attack interval 不短於 clip duration。
這代表素材 timing 同時是玩法 telegraph timing；修改 frame count／SPF 會改變 attack window，必須視為 gameplay change。

### 9.2 Melee

Melee 在事件幀才重新檢查 surface distance 與 wall LOS，而不是沿用起手瞬間的結果。玩家在 0.15 秒內退開
或進入遮擋，就不產生 damage event；本次 attack 仍視為已消費 event，不會在稍後補打。

### 9.3 Ranged muzzle 與瞄準

Ranged 在 0.10 秒事件使用當次 Update 的 player capsule center 作為 target，不鎖定起手時的舊位置。
CSV muzzle 以單格左上角為原點，轉換為 billboard world offset：

```text
horizontal = (muzzleX / frameWidth - 0.5) * renderWidth
vertical   = (1 - muzzleY / frameHeight) * renderHeight
world XZ   = enemy position + billboard-right * horizontal
world Y    = vertical
```

因此 muzzle 必須使用 render canvas，而不是 hitbox。Game 收到 ranged event 後建立固定方向 projectile；
它不追蹤玩家。由於本 frame 的 ProjectileSystem update 已在 event consumption 之前完成，新 projectile 從下一 frame
才開始移動，這是目前明確的一幀時序契約。

## 10. 每幀更新、同幀事件與死亡生命週期

目前 `Game.cpp` 的 stable Playing frame 順序為：

```text
Player movement
  -> EnemySystem update（可能排入 EnemyAttackEvent）
  -> WeaponController / consume ShotEvent / player hitscan
  -> existing ProjectileSystem update
  -> consume EnemyAttackEvent
  -> retire expired dead
  -> refill spawn slots
  -> door / camera / visual snapshot sync
```

### 10.1 同幀死亡事件問題

這個順序曾暴露一個重要問題：敵人可能先在 `EnemySystem::Update` 排入 attack event，之後被玩家同 frame
hitscan 擊殺，Game 最後才消費 event。如果不處理，屍體仍會傷害玩家或發射子彈。

現在 `MarkDead` 會先刪除該 enemy 尚未消費的 queued events，再進入 Dead。這保留「同幀反殺成功就取消攻擊」
的直覺語意，也有專門的 regression test。

### 10.2 Dead、active 與 occupied 是不同集合

- Dead 立即停止 AI、damage、cooldown、navigation 與 live collision。
- Dead 立即退出 `GetAliveCount()`，所以 wave active slot 可補位。
- Dead snapshot 仍保留，播放四格、共 0.40 秒的 dead clip。
- 顯示期間仍存在於 `CollectOccupiedColliders()`，不能在相同 marker 上生成新敵人。
- 0.40 秒到期後才 retire，Renderer 在下一次 Sync 移除 instance。

因此可見 instance 數可能短暫高於 active cap；這是為了死亡回饋與 spawn 不重疊而刻意接受的結果。

## 11. KamataEngine Constant-Buffer ABI Bug

### 11.1 問題本質

KamataEngine `Material::ConstBufferData` 尾端為：

```cpp
Vector3 uvScale;
Vector3 uvOffset;
```

在目前 Windows x64／MSVC layout：

| CPU 欄位 | Byte | HLSL register |
| --- | ---: | --- |
| `uvScale.xyz` | 48–59 | `c3.xyz` |
| `uvOffset.x` | 60–63 | `c3.w` |
| `uvOffset.yz` | 64–71 | `c4.xy` |

原始 HLSL 卻把完整 `float3 m_uv_offset` 放在 `c4`。HLSL `float3` 不跨 16-byte register，因此 GPU
從 byte 64 才開始讀，實際形成：

```text
GPU offset.x = CPU uvOffset.y
GPU offset.y = CPU uvOffset.z = 0
```

水平 frame offset 被 row Y 取代，垂直 row offset 消失；配合負 V scale 與 wrap sampler，活著的敵人可能取到
sheet 底部的 dead row。

### 11.2 這是不是既有 bug

**是。** CPU／HLSL layout 不一致在原 KamataEngine material interface 中已存在。過去大多數 material 的
`uvOffset=(0,0,0)`，即使讀錯仍得到零，所以沒有可見症狀。Enemy Atlas 是第一個持續使用非零 X／Y offset
的功能，讓潛伏 bug 顯現；第一版 Atlas 整合也因缺少 ABI guard 而產生回歸。

### 11.3 採用的修正

專案 `Obj.hlsli` 明確按實際 layout 拆分：

```hlsl
float3 m_uv_scale     : packoffset(c3);
float  m_uv_offset_x  : packoffset(c3.w);
float2 m_uv_offset_yz : packoffset(c4);
```

`ObjPS.hlsl` 再重建 `float2(m_uv_offset_x, m_uv_offset_yz.x)`。C++ 以 `static_assert` 驗證
`uvScale==48`、`uvOffset==60`；若引擎 ABI 改變，build 會失敗而不是靜默畫錯。

採用這個方案的理由是專案連結預編譯 library；局部 shader 修正能恢復公開 `uvOffset_` 的正常 X／Y 語意，
不必重建 engine 或新增整套 enemy D3D12 pipeline。

長期最乾淨的上游方案，是在 engine CPU struct 的 `uvScale` 後加入 4-byte padding，讓 `uvOffset` 從 byte 64
開始，然後重編 KamataEngine 與所有 consumers。詳細比較見
[EnemyAtlasShaderABI.zh-TW.md](EnemyAtlasShaderABI.zh-TW.md)。

## 12. Camera-Centered Sky Sphere

### 12.1 為何不是固定天空球或天花板

- 固定世界天空球實作簡單，但地圖擴建、傳送或長距離移動後可能被走出。
- 天花板適合封閉室內，卻把背景問題變成每張地圖的幾何與素材負擔，也限制可編輯高度。
- Cubemap skybox 是標準方案，但需要轉換現有 equirectangular PNG 與建立另一套 shader／asset 流程。
- Fullscreen inverse-view sky 沒有球體幾何，但對目前 KamataEngine MVP 的矩陣與 shader 整合成本較高。

因此採用 camera-centered sphere，直接使用既有 `sky_sphere.png`。

### 12.2 實作不變條件

- 半徑 50m、32×64 segments；camera far clip 為 100m，初始化要求 `farClip > radius`。
- 球心每 frame 等於 camera position，只跟隨 translation，不繼承 yaw／pitch。
- 沒有 collider，不參與 gameplay world。
- 從球內觀看，所以使用 front-face culling；無 blending。
- 以 read-only depth 繪製，天空寫 color 但不寫 depth。
- Draw order 為 Sky 先、Map 後；即使 map 物件距 camera 超過 50m，仍可通過未被天空寫入的 depth 並覆蓋 sky。

因此球面不是可見世界上限；真正上限仍是 camera far clip。

## 13. 場景偏暗、Material 與 Gamma

### 13.1 第一層：ambient-only／unlit-like world material

Map、Sky、Enemy、Projectile material 設為：

```text
ambient  = (1,1,1)
diffuse  = (0,0,0)
specular = (0,0,0)
alpha    = 1
```

這會移除 directional／point／spot diffuse 與 specular 對素材的再次壓暗，讓貼圖成為主要顏色來源。
但精確地說它不是獨立的 unlit shader：共用 `ObjPS` 仍乘 KamataEngine `ambientColor`，啟用 circle shadow 時
也可能扣減亮度。因此本報告稱它為 ambient-only 或 unlit-like。

### 13.2 第二層：world-only post-process

```text
Brightness = 1.25
Gamma      = 2.2

adjusted = pow(saturate(linearColor * Brightness), 2.2 / Gamma)
```

- Gamma 2.2 時 exponent 為 1，是本專案的中性相對曲線。
- Gamma 大於 2.2 時 exponent 小於 1，提高暗部。
- Gamma 小於 2.2 時增加對比並壓暗中低調。
- `saturate` 將輸出限制在 0–1，但也會裁切高光。

這個 Gamma 參數是「相對 tone adjustment」，不是要求 shader 再做一次傳統 `pow(color, 1/2.2)`。
sRGB render-target 硬體仍負責儲存／顯示 transfer function。

### 13.3 為何不直接改 PNG 或全 backbuffer

- 離線提亮 PNG 會破壞原始素材，難以一致回調。
- 只提高 directional light 會依賴法線與方向，sky／billboard 結果不一致。
- 在每個 object shader 分散加亮度容易漏掉未來世界效果。
- 對整個 backbuffer 後處理會連 HUD、UI、文字與 Fade 一起改色。

因此世界先離屏、overlay 後畫，是最符合需求的邊界。

## 14. 離屏渲染管線與 ScenePost Shaders

### 14.1 實際 draw pass

```text
Playing / Paused:
  DirectX PreDraw
    -> ScenePostProcessRenderer::BeginScene
         bind/clear offscreen color + private D32 depth
    -> Sky -> Map -> Enemy billboard -> Projectile
    -> ScenePostProcessRenderer::Composite
         sample scene, Brightness/Gamma, output to sRGB backbuffer
    -> Weapon/HUD
    -> Pause UI（僅 Paused）
    -> Screen Fade
    -> DirectX PostDraw

Main Menu / Results / other non-world screens:
  DirectX PreDraw -> UI -> Screen Fade -> DirectX PostDraw
```

HUD／UI 不受調色不是靠 mask，而是由 pass order 保證。

### 14.2 ScenePostVS.hlsl

Vertex shader 使用 `SV_VertexID` 產生一個 oversized fullscreen triangle：

- 不需要 vertex buffer 或 input layout。
- 三個 vertices 覆蓋 viewport。
- 相較兩個 triangles 的 fullscreen quad，避免中間 diagonal seam 與重複邊界插值。

### 14.3 ScenePostPS.hlsl

Pixel shader 從 `t0` 取樣 offscreen scene，透過兩個 root constants 取得 brightness／gamma，套用上述曲線，
並輸出 alpha 1。兩個 shader 在 renderer 初始化時以 Shader Model 5 動態編譯；缺檔或編譯失敗會使 Game
初始化失敗，而不是無聲跳過效果。

### 14.4 D3D12 resources 與狀態

| Resource／view | Format／用途 |
| --- | --- |
| scene color resource | `R8G8B8A8_TYPELESS` |
| scene RTV／SRV | `R8G8B8A8_UNORM_SRGB` |
| private scene depth／DSV | `D32_FLOAT` |
| SRV heap | 一個 shader-visible descriptor |
| Root parameter 0 | `t0` descriptor table |
| Root parameter 1 | brightness、gamma 兩個 32-bit constants |
| Sampler | static linear clamp |

Resource state invariant：

```text
跨 frame：PIXEL_SHADER_RESOURCE
BeginScene：PIXEL_SHADER_RESOURCE -> RENDER_TARGET
Composite：RENDER_TARGET -> PIXEL_SHADER_RESOURCE
```

`BeginScene`／`Composite` 有 paired-call guard，防止重入或未 Begin 就 Composite。

即使 fullscreen pass 關閉 depth test，KamataEngine `SetRenderTargets(true)` 仍同時綁定 engine 的 D32 DSV；
PSO 因而必須宣告 `DSVFormat=D32_FLOAT`。這是 D3D12 pipeline compatibility 要求，不代表 composite 使用 depth。

### 14.5 sRGB 的精確路徑

目前不是「整條 pipeline 只 encode 一次」，而是：

```text
world shader linear output
  -> offscreen sRGB RTV encode（8-bit storage）
  -> offscreen sRGB SRV decode
  -> linear Brightness/Gamma
  -> sRGB backbuffer final encode
```

中間 encode／decode 是互逆的色彩空間往返，因此不會形成 double Gamma；但會經過 8-bit 量化，且場景沒有 HDR
headroom。較長期可改用 `R16G16B16A16_FLOAT` linear offscreen target，再在 final composite tone-map／encode。

## 15. 資源部署、初始化與生命週期

- 原始資源位於 `NoviceResources/`，build 時完整部署到 `target/<Config>/Resources/`。
- CMake 將兩個 ScenePost shaders、四份 CSV、sky 與兩張 enemy sheets 列為 required resources；缺少時 configure 失敗。
- Runtime working directory 設為 executable directory，因此 `Resources/...` 路徑對 Debug／Release 一致。
- Sky、post-process、renderer 使用 PIMPL 與 RAII；初始化先建立暫時 `Impl`，全部成功才 commit。
- Debug build 啟用 DirectX debug layer；Release 不啟用。
- `Build.ps1` 會正規化 Windows 中重複的 `Path/PATH`，並優先選擇支援 Visual Studio 18 2026 generator 的
  Visual Studio bundled CMake；因此應使用該腳本，而不是假設 shell `PATH` 中的第一個 CMake 一定相容。
- Offscreen color／depth、viewport、scissor 只在初始化時建立。視窗 resize 目前被禁止；未來開放 resize 時必須重建它們。
- Composite 會綁自己的 shader-visible SRV heap；後續 Sprite／HUD pass 必須由各自 `PreDraw` 重新綁 pipeline 與 heap。

## 16. 過程中碰到的問題與解決結果

| 問題／風險現象 | 排除或定位方式 | 決定的解決方案 |
| --- | --- | --- |
| 可編輯地圖可能走出固定天空球 | 把天空視為 camera-relative background | Camera-centered、無 collider、read-only depth sphere |
| 原始素材偏暗 | 分離 material shading 與全場 tone | ambient-only world material + world-only post-process |
| Hitbox 與動畫 resolution 不一致 | 定義玩法形狀與 frame canvas 的不同語意 | CSV 分開 hitbox／render size，renderer 禁止回推 hitbox |
| 動作有大量透明留白 | 比較 alpha bounds 與固定 pivot 的代價 | 保留固定 canvas，不做逐格自動 trim |
| State 與畫面 row 完全錯配 | 驗證 state／CPU UV 後追到 byte ABI | HLSL 配合 `c3.w/c4.xy`，加 `static_assert` |
| Atlas 上下顛倒或左右鏡像 | 檢查 OBJ V 與 quad face orientation | 負 V scale、bottom offset、billboard yaw + π |
| 透明 quad 遮住後畫物件 | 區分 blend alpha 與 depth write | `clip(texcolor.a - 0.01)` |
| 大 delta 可能跳過事件時間 | 不比較浮點 exact equality | previous/current crossing + emitted flag |
| 敵人同 frame 死亡仍有 queued attack | 追蹤 Game 的實際 update order | `MarkDead` 刪除未消費 events |
| Fullscreen PSO 關 depth 仍有 DSV mismatch | 確認 engine 綁定的 render target set | PSO 宣告 D32 DSV format，DepthEnable 仍為 false |
| Runtime shader／CSV 找不到 | 追蹤 source 與 executable resource root | CMake required-resource preflight + config deployment |
| Shell CMake 太舊或 `Path/PATH` 重複使 MSBuild 失敗 | 區分 generator／process environment 錯誤與 compiler error | 使用 `Build.ps1` 選擇 VS bundled CMake 並正規化 process PATH |

## 17. 方案比較與最終理由

| 決策點 | 採用方案 | 未採用方案 | 理由 |
| --- | --- | --- | --- |
| 敵人素材資料 | 兩份關聯 CSV | 全寫死在 C++；單一超寬 CSV | 可驗證、可擴充，通用規則仍保持型別安全 |
| 透明留白 | 固定 frame canvas | 每格 alpha auto-trim | 保持 pivot、腳底與 muzzle 穩定 |
| 攻擊同步 | 0-based event frame | 起手立即傷害；動畫 notify 寫死在 state code | 素材可調 timing，大 delta 仍可測試 |
| Runtime 邊界 | event + snapshot | Renderer 直接讀／改 Enemy objects | 解除 Gameplay 與 KamataEngine 耦合 |
| Atlas GPU 傳遞 | 專案 HLSL ABI fix | CPU swizzle；33 textures；專用 pipeline | 對預編譯 engine 是最小且語意正確的修正 |
| 天空 | camera-centered sphere | 固定 sphere；ceiling；cubemap | 不會走出，直接使用現有 PNG，MVP 整合成本低 |
| 場景提亮 | ambient-only + world offscreen post | 改 PNG；加方向光；全 backbuffer post | 世界一致、overlay 保色、參數集中 |
| Scene target | 8-bit sRGB | HDR float target | 與現有 pipeline 相容、成本低；接受 MVP 量化限制 |

## 18. 驗證矩陣

### 18.1 Data tests

- 正確保留 hitbox／render／frame／clip／event／muzzle。
- 拒絕錯 header、unknown enemy、duplicate／missing state、零 frame、非法 timing。
- 拒絕 non-attack event、out-of-range event、muzzle 缺半邊／越界與 rectangle overflow。

### 18.2 Gameplay tests

- 不同 hitbox radius 影響 spawn、collider 與 navigation clearance。
- Melee 0.15 秒事件、蓄力閃避與 wall LOS。
- Ranged 0.10 秒事件、muzzle pixel conversion 與 current-player aim。
- Large delta 跨過事件仍只發一次。
- 同 frame lethal hit 取消 queued attack。
- Dead 立即退出 alive/collision，但 0.40 秒內仍 occupied，之後才 retire。

### 18.3 Rendering／shader contract tests

- Render size 與 hitbox height 分離。
- State-to-row、idle/move loop、attack/dead clamp。
- 首／尾 frame、half-texel、V 翻轉與 Atlas bounds。
- Alpha cutoff 與 KamataEngine material ABI source contract。
- Brightness／Gamma 參數、neutral exponent、black、中間調與 clamp。

### 18.4 Build 與 runtime 邊界

- 實作完成時 Debug／Release fresh build 與各自 CTest 均通過。
- 2026-08-28 文件定稿時以 `Build.ps1` 再次完成 Debug／Release build，並執行各自 CTest；
  `Object_FPS.Core` 均通過。
- 實際 GPU smoke test 已確認修正後存活 blood dog 使用 move row，並完成基本啟動驗證。

Headless tests 不能證明 sky seam／極點、mip bleed、透明 depth、billboard 左右、muzzle 肉眼位置、HUD layering、
sRGB view、descriptor heap、barrier 或 debug-layer 全部正確；這些仍屬人工 GPU regression。

## 19. 潛在風險與已知限制

| 風險 | 後果 | 現有緩解／後續方向 |
| --- | --- | --- |
| KamataEngine 將 `uvOffset` 改對齊 byte 64 | 專案 HLSL 再次不相容 | `static_assert` 阻止 build；升級時同步恢復完整 `float3@c4` |
| 專案 Obj shader 與 engine 原版分岔 | 同步／合併新版 KamataEngine Obj shader 時可能漏掉 ABI compatibility fix | 保留 ABI 文件、source contract test 與部署檢查 |
| Alpha cutoff 位於共用 ObjPS | 其他 OBJ 貼圖中 `texcolor.a < 0.01` 的 texel 也被 discard | 長期改 enemy 專用 alpha-tested pipeline |
| Atlas 無 gutter／texture array | 遠距 mip 可能 bleed | 素材加 padding、custom mip 或 texture array |
| 半透明 billboard 無排序 | 重疊透明可能不正確 | MVP 以 alpha-tested art 為主；未來加 sort／OIT |
| Event queue 只活一 frame | 延遲消費會遺失事件 | Game 固定同 frame 消費；若非同步需 owning queue／sequence ID |
| Clip timing 同時控制玩法 | 美術改 SPF 會改 telegraph／attack duration | Data review 將 timing 視為 gameplay change |
| Ranged projectile 下一 frame 才移動 | 固定一幀 latency | 保持契約或重排 spawn／projectile update |
| RuntimeEnemy 複製 definition | 大量 instance 時有額外記憶體 | 改 stable catalog handle，先處理 lifetime |
| `std::span` snapshot view | 跨 mutation 保存會失效 | 僅在同 frame immediate consumption 使用 |
| 33 materials／per-enemy draw | 敵人種類與數量大時擴展不佳 | Instance buffer、texture array 或 dedicated renderer |
| Draw 前改寫 shared model 的 mesh material pointer | 未來多執行緒 command recording／並行重用 model 時可能競態 | 單執行緒維持現狀；並行化時隔離 model／binding state |
| 8-bit offscreen encode/decode | 量化、banding、無 HDR headroom | 長期改 float RT + tone mapping |
| Offscreen color + depth 與 fullscreen pass | 增加約 7 MiB（1280×720）GPU memory 與一次全畫面頻寬成本 | MVP 可接受；高解析度時量測並考慮格式／解析度策略 |
| `saturate` 在 Gamma 前 | 高光裁切 | HDR／exposure／tone mapper |
| ScenePost 固定輸出 alpha 1 | 不保留 world scene alpha 供後續透明合成 | 目前 backbuffer composite 為不透明；未來有 alpha consumer 時重新設計 |
| Gamma 僅驗證 `>0` | 極端值可產生不實用曲線 | 未來設定合理 min/max |
| Ambient-only 並非真正 unlit | Global ambient／circle shadow 仍會影響 | 若需絕對一致，建立真正 unlit world shader |
| Offscreen 不支援 resize | 開放 resize 後尺寸失配 | resize event 重建 color/depth/views/viewport |
| KamataEngine backbuffer／DSV 契約改變 | Composite PSO format mismatch | Engine upgrade 時 GPU debug-layer regression |
| Sky equirectangular seam／極點與 winding | 接縫、變形或看不到內面 | 人工驗收；長期 cubemap／fullscreen sky |
| Loader 只允許一個 melee/ranged | 無法直接新增同類多 archetype | 擴充 spawn selection 與 catalog contract |

## 20. 維護與擴充指引

### 20.1 新增或更換 enemy sheet

1. 保持每個 enemy 的四個 clips 共用同一 frame pixel size。
2. `render_width / render_height` 描述完整 canvas，而非可見 alpha bounds。
3. 四個 state 必須各一列；只有 attack 可有 event，只有 ranged attack 可有 muzzle。
4. 檢查 event timing 是否仍符合 gameplay telegraph。
5. 在 GPU 上檢查左右朝向、腳底、muzzle、透明 edge 與遠距 mip bleed。

### 20.2 升級 KamataEngine

1. 先重新確認 `offsetof(Material::ConstBufferData, uvScale/uvOffset)`。
2. 檢查 engine 原版 `Obj.hlsli`、backbuffer sRGB 與 DSV format 契約。
3. 若 uvOffset 已移至 byte 64，連同 HLSL、`static_assert` 與 tests 一次更新。
4. 重新執行 Debug／Release build、CTest 與 runtime Atlas／post-process smoke test。

### 20.3 提升渲染品質

- 真正的 enemy／sky unlit shader，縮小共用 ObjPS 的影響面。
- Float HDR offscreen target、exposure、tone mapping 與可控 Gamma UI。
- Atlas gutter、custom mip 或 texture array。
- Resize-aware render-target recreation。
- GPU capture／debug-layer 自動驗收與 shader reflection ABI test。

## 21. 最終技術決策

- 使用 camera-centered 50m sky sphere；它是無碰撞、read-only-depth 的背景，不是世界邊界。
- 使用 ambient-only／unlit-like world materials，再以 world-only offscreen pass 套用 Brightness 1.25、Gamma 2.2。
- 使用固定 render canvas 保留共同座標系與 ground anchor，避免 runtime auto-trim 額外造成 pivot／腳底／muzzle
  跳動；素材內容本身仍須由美術對齊，hitbox 則完全獨立。
- 使用 CSV 管理 enemy／clip 差異，以程式保存 state、loop／clamp 與事件規則。
- 使用 0-based event frame 與 elapsed crossing，讓 melee 可閃避、ranged 在事件時瞄準，且大 delta 不漏事件。
- 使用短生命週期 events 傳遞行為、snapshots 傳遞只讀狀態，Renderer 以 definition ID 查靜態 cache。
- 對 KamataEngine 既有 material ABI bug 採專案 HLSL 相容修正與 compile-time guard；長期仍建議在 engine 上游補 padding。
- 接受 MVP 的 8-bit offscreen、每個 Atlas frame 對應的 cached material 與禁止 resize 限制，並將 HDR、專用 pipeline、texture array
  與 resize reconstruction 留作後續演進。

## 22. 主要實作與測試索引

| 主題 | 主要檔案 |
| --- | --- |
| Data definitions／loader | [GameData.hpp](../include/RetroFPS/Data/GameData.hpp)、[GameData.cpp](../src/Data/GameData.cpp) |
| Enemy CSV | [enemies.csv](../NoviceResources/data/enemies.csv)、[enemy_animation_clips.csv](../NoviceResources/data/enemy_animation_clips.csv) |
| Enemy runtime／events／snapshots | [EnemySystem.hpp](../include/RetroFPS/Gameplay/Enemy/EnemySystem.hpp)、[EnemySystem.cpp](../src/Gameplay/Enemy/EnemySystem.cpp) |
| Game orchestration／frame order | [Game.cpp](../src/Game/Game.cpp) |
| Atlas helpers／billboard renderer | [EnemyRenderSettings.hpp](../include/RetroFPS/Rendering/EnemyRenderSettings.hpp)、[EnemyBillboardRenderer.cpp](../src/Rendering/EnemyBillboardRenderer.cpp) |
| Kamata-compatible OBJ shaders | [Obj.hlsli](../NoviceResources/shaders/Obj.hlsli)、[ObjPS.hlsl](../NoviceResources/shaders/ObjPS.hlsl) |
| Sky | [SkySphereRenderer.hpp](../include/RetroFPS/Rendering/SkySphereRenderer.hpp)、[SkySphereRenderer.cpp](../src/Rendering/SkySphereRenderer.cpp) |
| Scene post-process | [ScenePostProcessSettings.hpp](../include/RetroFPS/Rendering/ScenePostProcessSettings.hpp)、[ScenePostProcessRenderer.cpp](../src/Rendering/ScenePostProcessRenderer.cpp) |
| Fullscreen shaders | [ScenePostVS.hlsl](../NoviceResources/shaders/ScenePostVS.hlsl)、[ScenePostPS.hlsl](../NoviceResources/shaders/ScenePostPS.hlsl) |
| Data／Gameplay／Rendering tests | [GameDataCatalogTests.cpp](../tests/Data/GameDataCatalogTests.cpp)、[EnemySystemTests.cpp](../tests/Gameplay/EnemySystemTests.cpp)、[EnemyBillboardTests.cpp](../tests/Rendering/EnemyBillboardTests.cpp)、[ScenePostProcessTests.cpp](../tests/Rendering/ScenePostProcessTests.cpp) |
