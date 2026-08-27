# Enemy Atlas Shader Constant-Buffer ABI 技術報告

- 文件狀態：Object_FPS 完整 MVP 的實作後分析
- 日期：2026-08-27
- 對應日本語版：[EnemyAtlasShaderABI.ja.md](EnemyAtlasShaderABI.ja.md)
- 主要相關檔案：
  - `NoviceResources/shaders/Obj.hlsli`
  - `NoviceResources/shaders/ObjPS.hlsl`
  - `src/Rendering/EnemyBillboardRenderer.cpp`
  - KamataEngine `External/KamataEngine/include/3d/Material.h`

## 1. 結論摘要

這次修改 `Obj.hlsli` 的直接理由，是 **GPU 端 HLSL constant buffer 與 CPU 端
`KamataEngine::Material::ConstBufferData` 的實際記憶體排列不一致**。

這個不一致並不是由敵人 CSV、敵人狀態機或 Atlas row 定義造成。敵人的
`idle / move / attack / dead` 狀態與 clip 選擇在 CPU 端原本就是正確的；錯誤發生在最後一步：
CPU 將正確的 Atlas UV offset 寫入 constant buffer 後，shader 從錯誤的 byte offset 讀取資料，
使 GPU 實際取樣到錯誤的 frame 與 row。

對「這是不是之前就存在的 bug」的精確回答是：

- **是，CPU／HLSL layout 不一致原先就存在，是 KamataEngine material 介面中的潛伏問題。**
- **但在本階段以前通常不會顯現**，因為既有 map、sky 與一般 OBJ material 的
  `uvOffset` 幾乎都是 `(0, 0, 0)`。讀錯零值仍然是零，所以畫面看起來正常。
- 敵人 Atlas 是第一個持續使用非零 X／Y UV offset 的功能，因此把這個潛伏問題顯性化。
- 第一版 Atlas 整合沒有先建立 CPU／GPU ABI 檢查，因而出現了可見回歸；本次修改是對該整合問題的正式修正。

本次採用專案內 shader 修正，是因為 Object_FPS 使用的是已編譯好的 KamataEngine library。
在不重編外部引擎、不另建完整敵人渲染 pipeline 的前提下，讓 HLSL 配合既有 CPU layout，
是變更面最小且語意最正確的方案。

## 2. 問題現象與非問題範圍

修正前可觀察到下列現象：

- 活著且正在移動的敵人，可能顯示 Atlas 最下方的死亡動作。
- `idle / move / attack / dead` 看起來與畫面上的動畫完全無關。
- frame index 改變時，不一定水平前進到下一格。
- 因為死亡動作通常較寬、較矮，錯取 row 也會被誤認為 render size 或 hitbox 尺寸錯誤。

以下部分實際上沒有發生對應錯配：

- CSV 中四個 state 的 `origin_y_px` 與 `frame_count`。
- `EnemyState` 到 `EnemyAnimationClipDefinition` 的 switch。
- attack／dead elapsed time 與 loop／clamp 規則。
- `render_width / render_height` 與 `hitbox_radius / hitbox_height` 的資料分離。

也就是說，CPU 端已經選出正確 state、clip 與 frame；錯誤是在 shader 取樣座標的最後階段發生。

## 3. 診斷與思考路徑

以下是本次可重現的工程診斷路徑。每一步都用來區分「狀態資料錯誤」「素材切格錯誤」與
「GPU 取樣介面錯誤」，而不是直接靠調整 CSV 猜測結果。

1. **從可見矛盾建立最小問題描述**
   畫面顯示的是 spitter 的死亡 pose，但該敵人仍存活、玩家沒有造成擊殺，runtime 應處於移動狀態。
   這表示問題至少可能位於 state、clip selection、UV calculation、material upload 或 shader sampling。

2. **先驗證資料與 state machine**
   逐一核對 CSV 的四列 origin、frame count、frame timing，以及
   `EnemyState -> EnemyAnimationClipDefinition` 的明確 switch。CPU snapshot 的 state／elapsed 也會原樣傳到 renderer。
   結果顯示 move state 確實選到 move clip，排除 CSV row 標籤與 AI state transition 錯配。

3. **檢查素材本體與透明範圍**
   驗證兩張 PNG 的實際 dimensions、每個 frame rectangle 與 alpha bounding box。
   大量透明留白確實會改變可見輪廓，但不可能把 move row 變成 dead row；因此 render size／hitbox 並非主要根因。

4. **驗證 CPU 計算出的 UV**
   檢查 half-texel inset、frame X offset、state Y offset、V 軸方向與 sheet bounds。
   純 CPU helper 對四個 state 都產生正確數值，表示錯誤發生在 helper 之後。

5. **沿著 material upload 追到 shader ABI**
   比較 KamataEngine `Material::ConstBufferData`、MSVC `offsetof` 結果與 `Obj.hlsli` 的 `packoffset`。
   此時發現 CPU `uvOffset` 從 byte 60 開始，但 HLSL `float3 m_uv_offset` 從 byte 64 開始。

6. **用 byte mapping 預測畫面症狀**
   根據 layout 推導，舊 shader 會把 intended Y offset 當作 U offset，並把 V offset 讀成零。
   這一結果同時預測了「水平 frame 不對」與「所有 state 落到錯誤 row」，與實際畫面吻合。

7. **選擇最小可驗證修正**
   在專案 HLSL 中把跨 register 的 `uvOffset` 拆為 `c3.w + c4.xy`，不改 CSV、不改 state machine，
   也不加入特殊個案 swizzle。若診斷正確，只修改 ABI 就應恢復所有 state。

8. **以反證方式驗收**
   修正後重新編譯 shader 與 Debug／Release，執行 state-to-row tests，並在實際 GPU 畫面觀察存活 blood dog
   連續播放 move row。原本的 dead-row 症狀消失，證實根因位於 constant-buffer layout。

這條路徑的核心判斷是：**透明留白能解釋可見大小差異，但不能解釋跨 row 的素材替換；只有 Y UV offset
遺失或錯讀能同時解釋所有症狀。**

## 4. 問題原因：CPU 與 GPU 的實際排列

### 4.1 CPU 端

KamataEngine 公開的 `Material::ConstBufferData` 尾端是兩個連續的 `Vector3`：

```cpp
Vector3 uvScale;
Vector3 uvOffset;
```

在目前 Object_FPS 的 Windows x64／MSVC 組態中，實際 offset 如下：

| 欄位 | Byte 範圍 | HLSL register 位置 |
| --- | ---: | --- |
| `uvScale.x/y/z` | 48–59 | `c3.xyz` |
| `uvOffset.x` | 60–63 | `c3.w` |
| `uvOffset.y` | 64–67 | `c4.x` |
| `uvOffset.z` | 68–71 | `c4.y` |

`EnemyBillboardRenderer.cpp` 目前以 `static_assert` 固定驗證：

```cpp
offsetof(KamataEngine::Material::ConstBufferData, uvScale) == 48
offsetof(KamataEngine::Material::ConstBufferData, uvOffset) == 60
```

因此這不是依靠推測的 layout，而是目前 build 會實際檢查的 ABI 條件。

### 4.2 修正前的 HLSL 端

原本的 `Obj.hlsli` 宣告為：

```hlsl
float3 m_uv_scale  : packoffset(c3);
float3 m_uv_offset : packoffset(c4);
```

HLSL 的一個 `float3` 不會跨越 16-byte register 邊界。第二個 `float3` 因而從 `c4.x`
（byte 64）開始，而不是 CPU `uvOffset.x` 所在的 byte 60。

當 CPU 寫入：

```text
uvOffset = (intendedOffsetX, intendedOffsetY, 0)
```

舊 shader 實際讀到的是：

```text
m_uv_offset.x = CPU uvOffset.y = intendedOffsetY
m_uv_offset.y = CPU uvOffset.z = 0
m_uv_offset.z = byte 72–75 的非契約資料
```

因此 pixel shader 的結果變成：

```text
U offset <- 錯讀成 row 的 Y offset
V offset <- 0
```

V offset 遺失後，Atlas 無法移動到指定 state row；配合負的 V scale 與 wrap sampler，
取樣容易落到 sheet 底部，這就是活著的敵人顯示 dead row 的直接原因。

## 5. 解決方案：本次修改內容

`Obj.hlsli` 現在明確依照 CPU 的跨 register 排列拆分欄位：

```hlsl
float3 m_uv_scale : packoffset(c3);
float  m_uv_offset_x : packoffset(c3.w);
float2 m_uv_offset_yz : packoffset(c4);
```

`ObjPS.hlsl` 再重建渲染需要的二維 offset：

```hlsl
const float2 uvOffset = float2(m_uv_offset_x, m_uv_offset_yz.x);
```

對應關係因而變為：

| Shader 值 | CPU 來源 | 結果 |
| --- | --- | --- |
| `uvOffset.x` | `uvOffset.x`，byte 60 | 正確的水平 frame offset |
| `uvOffset.y` | `uvOffset.y`，byte 64 | 正確的垂直 state row offset |

這項修正恢復了 `KamataEngine::Material::uvOffset_` 公開欄位原本應有的語意：
CPU 設定 `(x, y, z)`，GPU 就讀到相同的 `(x, y, z)`。

`ObjPS.hlsl` 中的 alpha cutoff 是另一項與 Atlas 透明區相關的修改；它避免透明 pixel 寫入 depth，
但不是 state／row 錯配的根因，兩者應分開理解。

## 6. 為什麼選擇修改專案內的 HLSL

本次選擇的考量如下：

1. Object_FPS 連結的是預先編譯好的 KamataEngine library，不能只修改外部 header 就改變 library 內部寫入方式。
2. 敵人 Atlas 已經使用 KamataEngine 公開的 `Material::uvScale_ / uvOffset_` 介面；CPU 端呼叫方式本身合理。
3. 專案會部署自己的 `NoviceResources/shaders`，所以修正只影響 Object_FPS，不會直接改寫共用 runtime 安裝目錄。
4. 修改兩個 shader 欄位即可恢復既有 Material API 的正確語意，不需要增加 root signature、descriptor 或同步機制。
5. map、sky 與其他既有 OBJ material 的 offset 為零，修正前後輸出相同；主要可見差異集中在 Atlas。

因此，對目前已完成的 MVP，專案內 HLSL ABI 修正是風險與成本最低的選擇。

## 7. 可行的替代技術方案

本次修改並不是唯一方案。以下方案都可行，但取捨不同。

| 方案 | 是否修改共用 OBJ shader | 優點 | 缺點／風險 | 本階段判斷 |
| --- | --- | --- | --- | --- |
| 專案內 HLSL 配合 CPU layout（本次方案） | 是 | 修改小、恢復 API 語意、無需重編引擎 | 未來引擎 ABI 改變時需同步更新 | MVP 最合適 |
| CPU 端刻意 swizzle，寫入 `{0, offsetX, offsetY}` | 否 | 修改行數最少 | 依賴已知錯位、破壞 `uvOffset_` 語意、可讀性差；引擎一修正就再次壞掉 | 不建議 |
| 為 Enemy 建立專用 shader／PSO／root signature／constant buffer | 否 | 完全隔離、不影響其他 Model | 大量 D3D12 pipeline、descriptor 與生命週期程式碼 | 可做但超出 MVP 必要性 |
| 每 frame 更新自訂 quad vertex UV | 否 | 不依賴 Material UV offset | 需要動態 vertex buffer 或多份 mesh，增加 GPU 同步與資源管理 | 不採用 |
| 把 Atlas 預先切成 33 張獨立 texture | 否 | 可維持 `(0,0)` offset，實作概念簡單 | 增加檔案、texture／descriptor、載入成本；違反單一 sheet 的資料設計 | 不採用 |
| 修正並重編 KamataEngine，在 CPU struct 加入 4-byte padding | 可保留舊 HLSL | 從引擎根源統一 `c4` layout，長期最乾淨 | 改變 library ABI，必須重編引擎與所有 consumer | 最佳上游方案，但不屬於本 MVP 範圍 |

### 7.1 不改 shader 的最小 workaround

由於舊 HLSL 的 `m_uv_offset.x/y` 實際讀取 CPU `uvOffset.y/z`，理論上可以在 C++ 改寫為：

```cpp
material->uvOffset_ = {0.0f, intendedOffsetX, intendedOffsetY};
```

這可以讓敵人 Atlas 暫時正常，而且不修改 shader。然而它只是利用錯誤 layout：

- `uvOffset_.x` 被故意丟棄。
- C++ 欄位名稱與 GPU 語意不一致。
- 其他使用正常 `(x, y, z)` 語意的 material 仍然有問題。
- KamataEngine 未來若修正 padding，這個 workaround 會立刻把 X／Y 再次錯置。

因此它適合作為短期診斷，不適合作為完整 MVP 的最終程式。

### 7.2 最理想的長期上游修正

如果未來可以維護並重編 KamataEngine，建議在 CPU constant-buffer struct 中明確加入 padding：

```cpp
Vector3 uvScale;
float uvScalePadding;
Vector3 uvOffset;
```

這會讓 `uvOffset` 從 byte 64／`c4.x` 開始，原本的 HLSL `float3 m_uv_offset : packoffset(c4)`
就能保持不變。這是較自然的 16-byte constant-buffer 設計，但必須同步更新 header、library 與所有使用者，
不能只改 Object_FPS 一側。

## 8. 影響範圍與相容性

### 8.1 有影響

- 使用 `Obj.hlsli / ObjPS.hlsl` 且設定非零 `Material::uvOffset_` 的 KamataEngine Model。
- 敵人 Atlas 的水平 frame 選擇與垂直 state row 選擇。

### 8.2 無玩法影響

- CSV state、frame count、event frame 與 muzzle 資料。
- Enemy AI、damage、hitbox、導航、attack timing 與 death timing。
- `render_width / render_height` 與 hitbox 分離規則。

### 8.3 無其他 pipeline 影響

- Sprite／HUD／UI shader。
- Scene post-process shader。
- Projectile gameplay。
- 使用零 UV offset 的 map／sky material；其輸入在修正前後都是零。

## 9. 驗證與回歸防線

本次不是只靠肉眼確認，已建立下列防線：

1. **編譯期 ABI 檢查**
   `EnemyBillboardRenderer.cpp` 以 `static_assert` 驗證 `uvScale == 48`、`uvOffset == 60`。

2. **Shader source contract 測試**
   測試確認 `c3.w / c4` 的拆分欄位存在，舊的 `float3 m_uv_offset : packoffset(c4)` 不存在。

3. **State-to-row 測試**
   測試涵蓋 `idle / move / attack / dead` 到對應 Atlas origin 與 frame 的映射。

4. **Shader 編譯**
   `ObjVS.hlsl` 與 `ObjPS.hlsl` 均通過 Shader Model 5 編譯。

5. **Fresh build 與 CTest**
   Debug／Release 均清除舊產物後完整編譯，兩組 CTest 通過。

6. **實際 GPU 畫面**
   修正前，存活敵人可顯示 spitter 的 dead frame；修正後，blood dog 在移動狀態連續顯示正確 move row。

## 10. 潛在風險

| 風險 | 可能後果 | 目前緩解方式 | 殘餘程度 |
| --- | --- | --- | --- |
| KamataEngine 未來把 `uvOffset` 對齊至 byte 64 | 現行 HLSL 會再次與新 ABI 不符 | `static_assert` 會在編譯期阻止不相容 build | 低，但升級引擎時必須處理 |
| 專案 shader 與 KamataEngine 原始 shader 分岔 | 更新 runtime shader 時可能覆蓋或漏合併修正 | 修正保存在版本控制、文件與 source contract test；build 後核對部署 hash | 中低 |
| `Obj.hlsli` 是共用 Model shader interface | 非敵人物件理論上也會受 layout 改變影響 | 零 offset 的 map／sky 在新舊 layout 下結果相同；Debug／Release 已做 smoke test | 低 |
| HLSL packing 依賴目前 Windows x64／MSVC ABI | 更換編譯器、平台或 Vector3 定義後 offset 可能不同 | 使用 `offsetof` 編譯期檢查，而不是只依賴文件假設 | 低 |
| Source-string test 不是完整 shader reflection | 字串正確不代表所有 binding 一定正確 | 同時執行 FXC 編譯、CPU mapping tests 與實際 GPU 驗收 | 低 |
| 只回退 HLSL、沒有回退 Atlas material upload | state／frame 錯列會立刻復發 | 文件明確要求替換整條 UV 傳遞方案，不可單獨移除修正 | 中 |
| 外部 library 實作改變，但 header offset 未改變 | `static_assert` 可能無法偵測 library 內部語意變更 | 引擎升級後保留 runtime Atlas smoke test | 低 |

效能方面，這次只是把同一份 constant-buffer 資料用正確欄位讀取，沒有增加 texture sample、draw call、
descriptor 或 GPU resource；新增的 `float2` 重建通常會被 shader compiler 直接最佳化，因此沒有實質效能風險。

alpha cutoff 另有邊緣鋸齒、遠距 mip bleed 與 `discard` 成本等一般透明材質風險，但它們屬於
`ObjPS.hlsl` 的透明處理，不是本次 `Obj.hlsli` ABI 修正本身的風險。

## 11. 未來維護注意事項

- 如果升級 KamataEngine，首先重新確認 `offsetof(Material::ConstBufferData, uvOffset)`。
- 若 offset 從 60 改為 64，代表引擎已加入 padding；此時應把 HLSL 恢復為完整的 `float3` at `c4`，
  並同步更新 `static_assert` 與測試。
- 目前的 `static_assert` 會讓不相容的引擎 ABI 在編譯期失敗，而不是悄悄產生錯誤畫面。
- 不應只移除 HLSL 修正；若要改用其他方案，必須同時替換 UV offset 的傳遞路徑。
- 更長期可以考慮以共享的 C++／HLSL layout 定義、shader reflection 或自動化 ABI 測試，避免兩端手動維護。

## 12. 最終判定

- **修改理由**：修正 CPU material constant buffer 與 GPU HLSL 的 byte layout 不一致，讓 Atlas X／Y offset 被正確讀取。
- **是否為之前存在的 bug**：是，但屬於原本被零 offset 掩蓋的潛伏 bug；敵人 Atlas 的非零 offset 讓它首次顯現。
- **是否有其他方案**：有，包括 CPU swizzle、專用 enemy pipeline、動態 UV、拆圖或重編 KamataEngine。
- **為何本次修改 HLSL**：在不重編外部引擎且保留單一 Atlas 的條件下，它是最小、最清楚且最符合既有 Material API 語意的修正。
