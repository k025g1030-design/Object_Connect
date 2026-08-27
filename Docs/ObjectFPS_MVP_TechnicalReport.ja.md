# Object_FPS 完成版 MVP 技術設計・実装レポート

- 文書ステータス：完成版 MVP 実装後の技術・診断・意思決定記録
- 日付：2026-08-27
- 繁体字中国語版：[ObjectFPS_MVP_TechnicalReport.zh-TW.md](ObjectFPS_MVP_TechnicalReport.zh-TW.md)
- KamataEngine ABI 詳細レポート：[EnemyAtlasShaderABI.ja.md](EnemyAtlasShaderABI.ja.md)
- アーキテクチャ参照：[Architecture.md](Architecture.md)

## 1. レポートの目的と結論

本レポートは、Object_FPS をプレイ可能な MVP から素材を用いた完成版へ拡張した際の技術判断を記録します。
対象は、データ駆動型 enemy、Atlas animation、イベントフレーム、event／snapshot 境界、死亡ライフサイクル、
sky sphere、シーン brightness／gamma、Direct3D 12 の offscreen 描画、および KamataEngine 統合時に
顕在化した constant-buffer ABI bug です。

最終設計は、次の五つの原則に要約できます。

1. **静的な差異は CSV、共通の振る舞いはプログラムで管理する。** Enemy size、asset、clip、timing、
   event frame はデータ、state の loop／clamp、event crossing、damage flow はプログラム規則です。
2. **Gameplay shape と render canvas を分離する。** Hitbox は texture size から決めず、billboard も
   hitbox height から scale しません。
3. **Simulation は短寿命 event で動作を出力し、snapshot で状態を公開する。** Renderer は AI や HP を所有しません。
4. **World 3D と overlay を別々に color adjustment する。** Sky／Map／Enemy／Projectile は offscreen scene に描き、
   HUD／UI／Fade は composite 後に原色で描きます。
5. **Engine ABI には検証可能な project-local compatibility layer を置く。** 現在は project HLSL を
   precompiled KamataEngine の実 CPU layout に合わせ、`static_assert` と tests で silent regression を防ぎます。

Enemy state が誤った row を表示した根因は CSV や state machine ではありませんでした。KamataEngine に以前から
存在し、ゼロ UV offset によって隠れていた CPU／GPU layout mismatch が、非ゼロ X／Y offset を常用する Enemy Atlas
によって初めて顕在化したものです。

## 2. 要件、制約、初期問題

### 2.1 機能要件

- Map を編集・拡張しても、player が sky background の外へ出ないこと。
- 既存の `sky_sphere.png` を利用し、MVP で新しい sky asset pipeline を作らないこと。
- 暗い world asset を一括調整しつつ、weapon、HUD、UI、text、Fade の色を変えないこと。
- 各 enemy は `idle / move / attack / dead` を一枚にまとめた Atlas を利用すること。
- Enemy hitbox、billboard size、frame pixel size、clip、attack event をデータとして管理すること。
- 各 animation frame には大きな透明余白があり、action ごとの alpha bounds は physics bounds と一致しないこと。
- Damage／projectile 発射は attack state 開始直後ではなく、指定 animation frame と同期すること。

### 2.2 技術制約

- Object_FPS は precompiled KamataEngine library を利用しており、外部 header だけを変更しても library の
  constant-buffer upload behavior は変えられません。
- Gameplay／Data は KamataEngine、Win32、D3D12 非依存を維持し、headless contract test を可能にします。
- 現段階では window resize を禁止しており、offscreen resource は初期 backbuffer size で一度だけ作成できます。
- Asset は equirectangular sky PNG と single enemy sheet であり、cubemap、texture array、asset baking tool は導入しません。
- MVP は少数 enemy を対象とし、ECS、汎用 event bus、render graph は先に導入しません。

### 2.3 一つに見えた三種類の問題

「enemy の size、animation、state がすべておかしい」という症状には、実際には三つの層がありました。

- **透明余白**：固定 frame canvas 内で可視キャラクターが一部しか占めず、小さい／ずれて見える。
- **Gameplay／render coupling**：hitbox height で billboard を描く、または alpha bounds を hitbox にすると双方が不正確になる。
- **Atlas row sampling**：CPU が正しい move clip を選んでも、GPU が dead row を sample する可能性がある。

この三つを分離しないまま CSV size を調整すると、shader ABI error を見た目だけで隠すことになります。

## 3. 全体レイヤーと責務境界

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

| Layer | 所有するもの | 所有しないもの |
| --- | --- | --- |
| Data | CSV parse、definition、foreign key、resource path validation | AI runtime、GPU resource |
| Gameplay/Enemy | state、navigation、HP、cooldown、event、snapshot | Player HP owner、projectile simulation、texture |
| Game | subsystem update order、event consumption、damage／projectile orchestration | Atlas UV、D3D12 descriptor |
| Rendering | texture／material cache、billboard、sky、post-process | AI transition、damage、spawn quota |
| Core | KamataEngine lifetime、frame boundary、window mode | Level content、draw content |

`Game` が composition root です。EnemySystem は `PlayerCombatState` や `ProjectileSystem` を include せず、
「melee／ranged attack が一回発生した」という event だけを出力します。Renderer は snapshot を読むだけで、
simulation を変更できません。

## 4. 診断・検討経路

### 4.1 最初に invariant を定義

本当の要件は「球を描く」「画像を明るくする」だけではありません。

- Sky は常に background であり、map boundary ではない。
- Hitbox は gameplay body、render canvas は asset canvas を表す。
- 一回の attack で event は最大一回、指定 frame と同期する。
- UI color は world brightness adjustment を受けない。
- CPU が設定した Atlas X／Y offset を GPU が同じ値として読む。

この invariant を先に固定することで、特定の画面だけ正常に見える workaround ではなく、比較可能な設計案を選べました。

### 4.2 Enemy 表示不具合の切り分け順序

1. 「生存・移動中なのに dead pose」という最小矛盾を定義する。
2. CSV state、origin、frame count、`EnemyState -> clip` switch を確認する。
3. Runtime snapshot の state／elapsed を確認し、AI が Dead に誤遷移していないことを確認する。
4. PNG dimensions、frame rectangles、transparent alpha ranges を調べる。
5. 透明余白は可視 size を説明できても move row を dead row に置換できない、と切り分ける。
6. CPU の half-texel、frame X、state Y、V-axis UV 計算を単体検証する。
7. `Material::uvOffset_ -> constant buffer -> Obj.hlsli -> ObjPS` の byte layout を追跡する。
8. 誤った byte mapping から「U に intended Y、V に zero」が入ると予測し、実症状と照合する。
9. CSV／state machine は変更せず ABI のみ修正し、全 state が復旧するか反証可能な形で確認する。

重要な判断は、**alpha padding は見た目の size 差を説明できますが、row 全体の置換は説明できず、row offset の
消失／誤読だけが全症状を同時に説明できる**という点です。

### 4.3 Brightness 問題の分解

Asset が暗い問題にも二層ありました。

1. 共通 OBJ shader の directional light が、すでに彩色済みの 2D art をさらに暗くする。
2. Diffuse／specular を除いても、world 全体に一貫した tone adjustment が必要である。
3. Backbuffer 全体を処理すると HUD／UI まで変色する。

そのため、world material を white ambient-only／unlit-like にした後、offscreen world scene のみに
Brightness／Gamma composite を適用しました。

### 4.4 Falsifiable test で収束

- Test definition を意図的に `hitboxHeight=3.5`、`renderHeight=0.8` とし、renderer が hitbox を使わないことを確認。
- 大きな `deltaSeconds` で event time を一度に跨ぎ、event が欠落・重複しないことを確認。
- 同 frame で enemy event を queue した後に kill し、queued event が cancel されることを確認。
- Atlas first／last frame、V flip、half-texel、out-of-bounds を検証。
- Gamma 2.2 の exponent が 1、black が black、Brightness が middle tone を上げることを検証。

## 5. データ駆動契約

### 5.1 Enemy CSV を二つに分けた理由

`enemies.csv` は「enemy type ごとに一行」の共通 definition、`enemy_animation_clips.csv` は
「enemy type と複数 state」の one-to-many data です。四 state をすべて横方向に入れると enemy row が肥大化し、
`(enemy_id, state)` 単位の duplicate／missing validation も困難になります。

`enemies.csv`：

```text
enemy_id, kind, damage, attack_interval_seconds, hp, defense,
hitbox_radius, hitbox_height, render_width, render_height,
texture_name, frame_width_px, frame_height_px
```

`enemy_animation_clips.csv`：

```text
enemy_id, state, origin_x_px, origin_y_px, frame_count,
seconds_per_frame, event_frame_index, muzzle_x_px, muzzle_y_px
```

### 5.2 Data と program rule の境界

| CSV で管理 | Program に保持 |
| --- | --- |
| enemy value、hitbox、render canvas、sheet、frame size | `EnemyState` の有限集合 |
| 各 clip の origin、frame count、SPF | idle／move loop、attack／dead clamp |
| attack event frame | event time crossing の once-only 判定 |
| ranged muzzle pixel | world conversion、LOS、damage／projectile rule |

これにより CSV を検証困難な script language にせず、asset author は enemy ごとの C++ 変更を不要にできます。

### 5.3 Load と cross validation

Loader は次を厳密に検証します。

- Exact header、UTF-8 BOM／LF／CRLF、quoted field、doubled quote。
- ID、kind、numeric range、duplicate ID、relative resource path、`..` rejection。
- Clip state は `idle / move / attack / dead` のみで、enemy ごとに四 state が一行ずつ必要。
- `frame_count > 0`、finite `seconds_per_frame > 0`。
- `event_frame_index` は 0-based、attack 専用、かつ `< frame_count`。
- Ranged attack muzzle X／Y は両方必要で frame 内、その他の row では muzzle 禁止。
- Rectangle arithmetic は先に `uint64_t` へ拡張し、64-bit／32-bit pixel overflow を拒否。
- Clip の `enemy_id` は既存 enemy definition を参照すること。

CSV layer では GPU に実際に load された PNG dimensions を確認できません。そのため Renderer が texture load 後に
`D3D12_RESOURCE_DESC` を使い、33 frame rectangles が実 sheet 内にあるか再検証します。これは semantic validation と
physical-resource validation の二層防御です。

現 loader は melee 一つ、ranged 一つを要求します。将来同 kind の複数 archetype を追加するには、CSV row の追加だけでなく
spawn selection policy と catalog contract の拡張が必要です。

## 6. Definition、Runtime、Snapshot、Event

四種類の data は lifetime が異なります。

| Type | Lifetime | 内容と用途 |
| --- | --- | --- |
| `EnemyDefinition` | Catalog／level lifetime | Static combat、hitbox、render、sheet、clips |
| `RuntimeEnemy` | Spawn から retire | state、HP、elapsed、cooldown、path、event flag、spawn 時 definition |
| `EnemySnapshot` | Update 後の read-only projection | ID、definition ID、state、position、hitbox、HP、flash、elapsed |
| `EnemyAttackEvent` | 現 Update から次 Update 前まで | origin、target、damage、attack identity |

### 6.1 Snapshot を採用した理由

Snapshot は texture path、frame pixels、clips を複製しません。Renderer は `definitionId` から初期化時の
definition／texture／material cache を参照し、simulation の state と elapsed だけで表示 frame を選びます。

利点は次のとおりです。

- Gameplay が KamataEngine／GPU resource に依存しない。
- Renderer が HP、AI、cooldown を変更できない。
- 毎 frame、全 enemy に asset data を複製しない。

正確には Snapshot は「definitionId だけ」ではありません。Hitscan、debug、renderer が必要とする runtime scalars も保持します。
また `RuntimeEnemy` は現在 full definition copy を持ち、spawn 時の data を固定しています。MVP scale では妥当ですが、
大規模化する場合は catalog-owned immutable handle に移行し、catalog lifetime／hot reload semantics を先に定義すべきです。

`GetSnapshots()` と `GetAttackEvents()` は `std::span` を返すため、次の system mutation を越えて view を保存できません。

### 6.2 Event-driven は汎用 asynchronous bus ではない

現在の event-driven は「system が frame ごとの短寿命 output queue を生成する」方式であり、cross-thread／persistent
message bus ではありません。`EnemySystem::Update` の開始時に前 frame の events は clear されるため、Game は次 Update 前に
consume する必要があります。Weapon も同じく `ShotEvent` を出力し、Game が hitscan、damage、tracer を解決します。

## 7. Hitbox と Render Canvas の分離

現在の data：

| Enemy | Hitbox radius | Hitbox height | Render width | Render height | Frame pixels |
| --- | ---: | ---: | ---: | ---: | ---: |
| `melee_basic` | 0.20m | 0.80m | 0.973913m | 0.80m | 560×460 |
| `ranged_basic` | 0.20m | 1.60m | 1.230769m | 1.60m | 700×910 |

- Spawn safety、navigation clearance、surface distance、live collider、hitscan capsule は hitbox を使用。
- Billboard scale は `render_width / render_height` のみを使用。
- Billboard center Y は `render_height / 2` で、full canvas の bottom edge を ground に合わせる。

Render size は「透明余白を含む固定 frame canvas の world size」であり、visible alpha bounding box ではありません。
全 action が同じ canvas、pivot、ground anchor を保つため、frame ごとの auto-trim による scale、foot position、muzzle の揺れを防げます。

一方、透明余白が大きい frame では可視キャラクターが小さく見えます。将来 crop が必要な場合は、CSV に per-clip trim rect、
pivot／ground anchor、visible scale を追加すべきです。Frame ごとの alpha auto-bounds は animation jitter を起こすため不適切です。

## 8. Atlas Animation と Billboard 技術

### 8.1 State と frame rule

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

Attack／Dead は最後の frame を保持し、retire／state transition 待ちの間に first frame へ戻らないようにします。

### 8.2 UV、V-axis、half-texel

各 cell の left は `origin + frameWidth * frameIndex` です。Linear sampling が neighboring cell へ滲まないよう、
UV endpoints は texel center に置きます。

```text
offsetX = (left + 0.5) / sheetWidth
offsetY = (bottom - 0.5) / sheetHeight
scaleX  = (frameWidth - 1) / sheetWidth
scaleY  = -(frameHeight - 1) / sheetHeight
```

共有 `map_wall.obj` は KamataEngine OBJ loader を通るため、quad top edge の V direction と Atlas の top-left coordinate が
異なります。Frame bottom pixel center から開始し、negative `scaleY` を使うことで asset を正立させます。

Billboard yaw は enemy から viewer への方向で算出します。共有 quad の表面をそのまま向けると U が mirror されるため、
yaw に π を加え、culling disabled で正しい面を表示します。これは visual orientation correction であり、AI facing state ではありません。

### 8.3 Cache と draw

- 一つの shared quad model。
- Definition ID ごとに二枚の sheet texture を cache。
- Melee 17 frames、ranged 16 frames、合計 33 immutable materials。
- 各 material は UV scale／offset を保持し、Draw 時に snapshot state＋elapsed から選択。
- Instance は identity、state／elapsed、Object3d、ObjectColor、billboard yaw のみを保持。

同一 material constant buffer を毎 frame 書き換えずに済みますが、frame／enemy type が増えると material 数と draw calls が
線形増加します。将来は texture array、instance data、enemy-specific pipeline が候補です。

### 8.4 Transparent depth と hit flash

Alpha blending だけでは、ほぼ透明な quad pixel も depth を書き、後で描く object を隠すことがあります。
共通 `ObjPS.hlsl` は sample 後に次を実行します。

```hlsl
clip(texcolor.a - 0.01f);
```

これは transparent Atlas texel の depth occlusion を解決しますが、state／row mismatch の根因ではありません。
また共通 OBJ shader のため、すべての OBJ で alpha `<0.01` が discard され、semi-transparent sorting は未解決です。

通常 enemy color は white、hit 時は `ObjectColor` brightness を約 1.5 倍へ 0.12 秒だけ上げます。
Melee red／ranged blue tint は廃止し、original art を表示します。

## 9. イベントフレーム駆動 Attack

### 9.1 Timing semantics

`event_frame_index` は 0-based です。

```text
eventTime = eventFrameIndex * secondsPerFrame
```

| Enemy | Attack frames | SPF | Event index | Event time | Attack duration |
| --- | ---: | ---: | ---: | ---: | ---: |
| melee | 6 | 0.05s | 3 | 0.15s | 0.30s |
| ranged | 5 | 0.05s | 2 | 0.10s | 0.25s |

Attack entry で elapsed を zero、`attackEventEmitted=false` とし、attack interval cooldown は wind-up 開始時から進めます。
Event index 0 は entry 時に即時発火し、それ以外は次で判定します。

```text
previousElapsed <= eventTime && currentElapsed >= eventTime && !emitted
```

Floating-point time が eventTime と完全一致することを要求しないため、frame hitch／large delta で 0.15 秒を一度に跨いでも
一回だけ発火し、`attackEventEmitted` が重複を防ぎます。

Attack duration は `frameCount * SPF` から算出され、attack interval は clip duration 以上でなければなりません。
したがって art timing は gameplay telegraph timing でもあり、frame count／SPF 変更は gameplay change として review が必要です。

### 9.2 Melee

Melee は event frame で surface distance と wall LOS を再確認します。Wind-up 開始時の結果を固定しないため、player が
0.15 秒以内に離れる、または遮蔽物へ入ると damage event は発生しません。その attack の event は消費済みとなり、後から追撃しません。

### 9.3 Ranged muzzle と aim

Ranged は 0.10 秒の event 時点で、その Update の player capsule center を target にします。Attack entry 時の旧位置には lock しません。
CSV muzzle は frame-local top-left origin で、billboard world offset へ次のように変換します。

```text
horizontal = (muzzleX / frameWidth - 0.5) * renderWidth
vertical   = (1 - muzzleY / frameHeight) * renderHeight
world XZ   = enemy position + billboard-right * horizontal
world Y    = vertical
```

Muzzle conversion は hitbox ではなく render canvas を使います。Game が ranged event を受けると fixed-direction projectile を作成し、
projectile は player を追尾しません。本 frame の ProjectileSystem update は event consumption より前に終了しているため、
新 projectile は次 frame から移動します。これは現実装の明示的な one-frame timing contract です。

## 10. Frame Update、Same-frame Event、Death Lifecycle

現在の `Game.cpp` における stable Playing frame の実順序：

```text
Player movement
  -> EnemySystem update（EnemyAttackEvent を queue する可能性）
  -> WeaponController / ShotEvent consumption / player hitscan
  -> existing ProjectileSystem update
  -> EnemyAttackEvent consumption
  -> expired dead retirement
  -> spawn slot refill
  -> door / camera / visual snapshot sync
```

### 10.1 Same-frame death event 問題

この順序では、enemy が `EnemySystem::Update` で attack event を queue した後、同 frame の player hitscan で kill され、
最後に Game が event を consume する可能性があります。対策がなければ dead enemy が damage／projectile を発生させます。

現在 `MarkDead` は、その enemy の未消費 queued events を先に削除してから Dead へ遷移します。
これにより「同 frame の反撃前に倒せたら attack は cancel」という直感的 semantics を保ち、専用 regression test もあります。

### 10.2 Dead、active、occupied は別集合

- Dead は即座に AI、damage、cooldown、navigation、live collision を停止。
- Dead は即座に `GetAliveCount()` から外れ、wave active slot は refill 可能。
- Dead snapshot は残り、四 frame、合計 0.40 秒の dead clip を表示。
- 表示中は `CollectOccupiedColliders()` に残り、同一 marker への respawn を禁止。
- 0.40 秒後に retire し、次 Renderer Sync で instance を削除。

このため visible instance 数が一時的に active cap を超えることがあります。Death feedback と spawn overlap prevention のために
意図的に受け入れた結果です。

## 11. KamataEngine Constant-Buffer ABI Bug

### 11.1 問題の本質

KamataEngine `Material::ConstBufferData` の末尾：

```cpp
Vector3 uvScale;
Vector3 uvOffset;
```

現在の Windows x64／MSVC layout：

| CPU field | Byte | HLSL register |
| --- | ---: | --- |
| `uvScale.xyz` | 48–59 | `c3.xyz` |
| `uvOffset.x` | 60–63 | `c3.w` |
| `uvOffset.yz` | 64–71 | `c4.xy` |

元の HLSL は full `float3 m_uv_offset` を `c4` に置いていました。HLSL `float3` は 16-byte register を跨がないため、
GPU は byte 64 から読みます。

```text
GPU offset.x = CPU uvOffset.y
GPU offset.y = CPU uvOffset.z = 0
```

Horizontal frame offset が row Y に置き換わり、vertical row offset は失われます。Negative V scale と wrap sampler の組み合わせで、
live enemy が sheet bottom の dead row を sample することがありました。

### 11.2 以前から存在した bug か

**はい。** CPU／HLSL layout mismatch は元の KamataEngine material interface に潜在していました。従来の material は
`uvOffset=(0,0,0)` が多く、誤った場所から zero を読んでも見た目に現れませんでした。Enemy Atlas が非ゼロ X／Y offset を
継続利用したため初めて顕在化し、初回 Atlas integration に ABI guard がなかったことも regression を許しました。

### 11.3 採用した修正

Project `Obj.hlsli` を実 layout に合わせて分割します。

```hlsl
float3 m_uv_scale     : packoffset(c3);
float  m_uv_offset_x  : packoffset(c3.w);
float2 m_uv_offset_yz : packoffset(c4);
```

`ObjPS.hlsl` で `float2(m_uv_offset_x, m_uv_offset_yz.x)` を再構築します。C++ の `static_assert` は
`uvScale==48`、`uvOffset==60` を保証し、engine ABI 変更時に silent visual bug ではなく build failure にします。

Project は precompiled library を link しているため、この local HLSL fix が public `uvOffset_` の X／Y semantics を回復する
最小の修正でした。Engine rebuild や enemy 専用 D3D12 pipeline は不要です。

長期的に最も明快な upstream fix は、engine CPU struct の `uvScale` 後に 4-byte padding を入れ、`uvOffset` を byte 64 から
開始させて KamataEngine と全 consumer を rebuild することです。詳細は
[EnemyAtlasShaderABI.ja.md](EnemyAtlasShaderABI.ja.md) を参照してください。

## 12. カメラ追従型 Sky Sphere

### 12.1 Fixed sphere／ceiling を採用しなかった理由

- World-fixed sky sphere は単純ですが、map expansion、teleport、long-distance movement で外へ出られます。
- Ceiling は indoor map には適しますが、background 問題を各 map の geometry／asset burden に変え、編集可能 height を制限します。
- Cubemap skybox は標準的ですが、既存 equirectangular PNG の変換と別 shader／asset flow が必要です。
- Fullscreen inverse-view sky は geometry 不要ですが、現 KamataEngine MVP では matrix／shader integration cost が高くなります。

そこで既存 `sky_sphere.png` をそのまま利用できる camera-centered sphere を採用しました。

### 12.2 実装 invariant

- Radius 50m、32×64 segments、camera far clip 100m。Initialization は `farClip > radius` を要求。
- Sphere center は毎 frame camera position と同一で、translation のみ追従し yaw／pitch は継承しない。
- Collider を持たず、gameplay world に参加しない。
- 内側から見るため front-face culling、blending none。
- Depth read-only で、sky は color を書くが depth を書かない。
- Sky を先、Map を後に描く。Map object が camera から 50m より遠くても、sky が depth を書かないため Map が上書き可能。

したがって sphere surface は visible-world limit ではなく、実際の限界は camera far clip です。

## 13. 暗い Scene、Material、Gamma

### 13.1 第一層：ambient-only／unlit-like world material

Map、Sky、Enemy、Projectile material：

```text
ambient  = (1,1,1)
diffuse  = (0,0,0)
specular = (0,0,0)
alpha    = 1
```

これにより directional／point／spot diffuse と specular が art を再び暗くすることを防ぎ、texture を主 color source にします。
ただし厳密には独立 unlit shader ではありません。共通 `ObjPS` は KamataEngine `ambientColor` を乗算し、circle shadow が有効なら
brightness を減算できます。そのため本レポートでは ambient-only／unlit-like と表現します。

### 13.2 第二層：world-only post-process

```text
Brightness = 1.25
Gamma      = 2.2

adjusted = pow(saturate(linearColor * Brightness), 2.2 / Gamma)
```

- Gamma 2.2 は exponent 1 で、本 project における neutral relative curve。
- Gamma > 2.2 は exponent < 1 となり、dark tones を持ち上げる。
- Gamma < 2.2 は contrast を増し、中間／暗部を下げる。
- `saturate` は 0–1 に clamp するが highlight clipping を起こす。

この Gamma は relative tone adjustment であり、shader で traditional `pow(color, 1/2.2)` を重ねる意味ではありません。
sRGB render-target hardware が storage／display transfer function を担当します。

### 13.3 PNG 編集／full-backbuffer post を採用しなかった理由

- Offline PNG brightening は source asset を破壊し、一括 revert／tuning が困難。
- Directional light 増加だけでは normals／direction に依存し、sky／billboard が一致しない。
- Object shader ごとに brightness を分散すると future world effect を漏らしやすい。
- Backbuffer 全体を処理すると HUD、UI、text、Fade まで変色する。

そのため world を先に offscreen、overlay を後に描く境界を採用しました。

## 14. Offscreen Rendering Pipeline と ScenePost Shaders

### 14.1 実際の draw pass

```text
DirectX PreDraw
  -> ScenePostProcessRenderer::BeginScene
       bind/clear offscreen color + private D32 depth
  -> Sky
  -> Map
  -> Enemy billboard
  -> Projectile
  -> ScenePostProcessRenderer::Composite
       sample scene, Brightness/Gamma, output to sRGB backbuffer
  -> Weapon/HUD
  -> Pause/Menu/Results UI
  -> Screen Fade
  -> DirectX PostDraw
```

HUD／UI が color adjustment を受けないことは mask ではなく pass order で保証されます。

### 14.2 ScenePostVS.hlsl

Vertex shader は `SV_VertexID` から oversized fullscreen triangle を生成します。

- Vertex buffer／input layout 不要。
- 三 vertices で viewport 全体を cover。
- Two-triangle fullscreen quad の diagonal seam／重複 edge interpolation を避ける。

### 14.3 ScenePostPS.hlsl

Pixel shader は `t0` の offscreen scene を sample し、二つの root constants から brightness／gamma を受け取り、
curve を適用して alpha 1 を出力します。Shaders は renderer initialization 時に Shader Model 5 で runtime compile され、
missing file／compile failure は Game initialization failure になります。Effect を黙って無効化しません。

### 14.4 D3D12 resources と states

| Resource／view | Format／purpose |
| --- | --- |
| scene color resource | `R8G8B8A8_TYPELESS` |
| scene RTV／SRV | `R8G8B8A8_UNORM_SRGB` |
| private scene depth／DSV | `D32_FLOAT` |
| SRV heap | shader-visible descriptor 一つ |
| Root parameter 0 | `t0` descriptor table |
| Root parameter 1 | brightness／gamma の 32-bit constants 二つ |
| Sampler | static linear clamp |

Resource-state invariant：

```text
Across frames：PIXEL_SHADER_RESOURCE
BeginScene：PIXEL_SHADER_RESOURCE -> RENDER_TARGET
Composite：RENDER_TARGET -> PIXEL_SHADER_RESOURCE
```

`BeginScene`／`Composite` は paired-call guard を持ち、double begin／begin-less composite を拒否します。

Fullscreen pass の depth test は無効ですが、KamataEngine `SetRenderTargets(true)` は engine D32 DSV も同時に bind します。
そのため PSO は `DSVFormat=D32_FLOAT` を宣言する必要があります。これは D3D12 pipeline compatibility のためであり、
composite が depth test を使うという意味ではありません。

### 14.5 sRGB の正確な経路

現在の pipeline は「全体で encode が一回だけ」ではありません。

```text
world shader linear output
  -> offscreen sRGB RTV encode（8-bit storage）
  -> offscreen sRGB SRV decode
  -> linear Brightness/Gamma
  -> sRGB backbuffer final encode
```

中間 encode／decode は inverse color-space round trip なので double Gamma にはなりませんが、8-bit quantization を通り、
HDR headroom はありません。長期的には `R16G16B16A16_FLOAT` linear offscreen target と final tone mapping が改善案です。

## 15. Resource Deployment、Initialization、Lifetime

- Source resource は `NoviceResources/` にあり、build 時に `target/<Config>/Resources/` へ完全 deploy。
- CMake は二つの ScenePost shaders、四 CSV、sky、二 enemy sheets を required resources とし、missing 時は configure failure。
- Runtime working directory は executable directory で、Debug／Release とも `Resources/...` path が同じ。
- Sky、post-process、renderers は PIMPL／RAII を使い、temporary `Impl` を完全初期化してから commit。
- Debug build は DirectX debug layer を有効化し、Release は無効。
- Offscreen color／depth、viewport、scissor は initialization 時に一度作成。Resize を許可する場合は再構築が必要。
- Composite は独自 shader-visible SRV heap を bind するため、後続 Sprite／HUD pass は各 `PreDraw` で pipeline／heap を再 bind。

## 16. 開発中に発生した問題と解決

| 問題／想定リスク | 切り分け方法 | 採用した解決策 |
| --- | --- | --- |
| Editable map で fixed sky sphere の外へ出る | Sky を camera-relative background と定義 | Camera-centered、no collider、depth read-only sphere |
| Original asset が暗い | Material shading と global tone を分離 | ambient-only world material + world-only post-process |
| Hitbox と animation resolution が異なる | Gameplay shape と frame canvas の意味を分離 | CSV で hitbox／render size を分け、renderer は hitbox から逆算しない |
| Action に大きな transparent padding | Alpha bounds と stable pivot の trade-off を比較 | Fixed canvas を維持し、per-frame auto-trim を行わない |
| State と visual row が完全に不一致 | State／CPU UV 検証後に byte ABI まで追跡 | HLSL を `c3.w/c4.xy` に合わせ、`static_assert` 追加 |
| Atlas が上下反転／左右 mirror | OBJ V と quad face orientation を検証 | negative V scale、bottom offset、billboard yaw + π |
| Transparent quad が後描画 object を隠す | Blend alpha と depth write を分けて確認 | `clip(alpha - 0.01)` |
| Large delta で event time を飛び越える | Float exact equality を使わない | previous/current crossing + emitted flag |
| Same-frame kill 後にも queued attack が残る | Game の実 update order を追跡 | `MarkDead` が unconsumed event を削除 |
| Fullscreen PSO は depth off でも DSV mismatch | Engine が bind する target set を確認 | PSO に D32 DSV format、`DepthEnable=false` |
| Runtime shader／CSV missing | Source と executable resource root を追跡 | CMake required-resource preflight + per-config deploy |

## 17. 方式比較と採用理由

| Decision | 採用方式 | 不採用方式 | 理由 |
| --- | --- | --- | --- |
| Enemy asset data | 関連する二 CSV | C++ hard-code、単一巨大 CSV | Validatable／extensible、共通規則は type-safe |
| Transparent padding | Fixed frame canvas | Per-frame alpha auto-trim | Pivot、feet、muzzle を安定化 |
| Attack sync | 0-based event frame | Attack entry で即 damage、state code に notify hard-code | Asset timing を調整可能、large delta に強い |
| Runtime boundary | event + snapshot | Renderer が Enemy object を直接 read／write | Gameplay と KamataEngine を分離 |
| Atlas GPU path | Project HLSL ABI fix | CPU swizzle、33 textures、dedicated pipeline | Precompiled engine に対する最小で semantic な修正 |
| Sky | Camera-centered sphere | Fixed sphere、ceiling、cubemap | 外へ出ない、existing PNG を利用、MVP cost が低い |
| Scene brightness | ambient-only + world offscreen post | PNG edit、directional light、full-backbuffer post | World は一貫、overlay は原色、parameter 集中 |
| Scene target | 8-bit sRGB | HDR float target | Existing pipeline compatibility／低 cost、MVP quantization を受容 |

## 18. 検証マトリクス

### 18.1 Data tests

- Hitbox／render／frame／clip／event／muzzle data の保持。
- Wrong header、unknown enemy、duplicate／missing state、zero frame、invalid timing の rejection。
- Non-attack event、out-of-range event、partial／out-of-frame muzzle、rectangle overflow の rejection。

### 18.2 Gameplay tests

- Definition hitbox radius が spawn、collider、navigation clearance を変える。
- Melee 0.15s event、wind-up dodge、wall LOS。
- Ranged 0.10s event、muzzle conversion、current-player aim。
- Large delta crossing でも event は一回。
- Same-frame lethal hit が queued attack を cancel。
- Dead は即 alive／collision から外れ、0.40s は occupied、その後 retire。

### 18.3 Rendering／shader contract tests

- Render size と hitbox height の分離。
- State-to-row、idle/move loop、attack/dead clamp。
- First／last frame、half-texel、V flip、Atlas bounds。
- Alpha cutoff と KamataEngine material ABI source contract。
- Brightness／Gamma validation、neutral exponent、black、middle tone、clamp。

### 18.4 Build と runtime boundary

- 実装完了時に Debug／Release fresh build と各 CTest が成功。
- 2026-08-27 の文書作成時にも Debug／Release CTest を再実行し、`Object_FPS.Core` は両方成功。
- GPU smoke test で修正後の live blood dog が move row を表示し、基本起動も確認済み。

Headless tests は sky seam／poles、mip bleed、transparent depth、billboard handedness、visual muzzle position、HUD layering、
sRGB view、descriptor heap、barrier、debug-layer warnings を証明できません。これらは manual GPU regression の対象です。

## 19. 潜在リスクと既知の制約

| Risk | Result | 現在の mitigation／future direction |
| --- | --- | --- |
| KamataEngine が `uvOffset` を byte 64 に align | Project HLSL が再び mismatch | `static_assert` で build stop、upgrade 時に full `float3@c4` へ同期 |
| Project Obj shader が engine original から diverge | Runtime update が fix を上書き | ABI docs、source contract test、deployment check |
| Alpha cutoff が共通 ObjPS にある | 他 OBJ の low alpha も discard | 将来 enemy-specific alpha-tested pipeline |
| Atlas に gutter／texture array がない | Distant mip bleed | Asset padding、custom mip、texture array |
| Semi-transparent billboard sorting なし | Overlap transparency error | MVP は alpha-tested art、将来 sort／OIT |
| Event queue は一 frame lifetime | Late consumer が event loss | Game が same-frame consume、async 化時は owning queue／sequence ID |
| Clip timing が gameplay も制御 | Artist の SPF 変更が attack behavior を変更 | Timing を gameplay change として data review |
| Ranged projectile は次 frame から移動 | Fixed one-frame latency | Contract を維持、または spawn／projectile order を変更 |
| RuntimeEnemy が definition copy | 大量 instance で追加 memory | Lifetime を保証した stable catalog handle |
| `std::span` snapshot view | Mutation を跨ぐと invalid | Same-frame immediate consumption のみ |
| 33 materials／per-enemy draw | Type／instance 増加で scalability 低下 | Instance buffer、texture array、dedicated renderer |
| 8-bit offscreen encode/decode | Quantization、banding、no HDR headroom | Float RT + tone mapping |
| Gamma 前の `saturate` | Highlight clipping | HDR／exposure／tone mapper |
| Gamma validation は `>0` のみ | Extreme curve が設定可能 | Practical min/max を追加 |
| Ambient-only は true unlit ではない | Global ambient／circle shadow が影響 | 必要時に true unlit world shader |
| Offscreen は resize 非対応 | Resize 後に dimensions mismatch | Resize event で resources／views／viewport 再作成 |
| KamataEngine backbuffer／DSV contract change | Composite PSO format mismatch | Engine upgrade 時に GPU debug-layer regression |
| Sky equirectangular seam／poles／winding | Seam、distortion、inside face failure | Manual check、長期 cubemap／fullscreen sky |
| Loader は one melee／one ranged のみ | Same-kind multi-archetype を追加不可 | Spawn selection と catalog contract を拡張 |

## 20. 保守・拡張ガイド

### 20.1 Enemy sheet の追加／交換

1. Enemy ごとに四 clips が同一 frame pixel size を共有すること。
2. `render_width / render_height` は visible alpha bounds ではなく full canvas を表すこと。
3. 四 state を一行ずつ用意し、event は attack のみ、muzzle は ranged attack のみに設定すること。
4. Event timing が gameplay telegraph と一致するか review すること。
5. GPU 上で handedness、feet、muzzle、transparent edge、distant mip bleed を確認すること。

### 20.2 KamataEngine upgrade

1. `offsetof(Material::ConstBufferData, uvScale/uvOffset)` を最初に再確認。
2. Engine original `Obj.hlsli`、backbuffer sRGB、DSV format contract を確認。
3. uvOffset が byte 64 に変わった場合、HLSL、`static_assert`、tests を同時更新。
4. Debug／Release build、CTest、runtime Atlas／post-process smoke test を再実行。

### 20.3 Rendering quality の改善

- True enemy／sky unlit shader を導入し、共通 ObjPS の影響範囲を縮小。
- Float HDR offscreen target、exposure、tone mapping、runtime Gamma UI。
- Atlas gutter、custom mip、texture array。
- Resize-aware render-target recreation。
- GPU capture／debug-layer automation、shader reflection ABI test。

## 21. 最終技術決定

- Camera-centered 50m sky sphere を使用し、no collision／depth read-only background として world boundary から分離。
- Ambient-only／unlit-like world material の後、world-only offscreen pass で Brightness 1.25、Gamma 2.2 を適用。
- Fixed render canvas で transparent padding、pivot、feet、muzzle を安定させ、hitbox は完全に独立。
- Enemy／clip の差異は CSV、state／loop／clamp／event rule は program に保持。
- 0-based event frame と elapsed crossing により melee dodge、event-time ranged aim、large-delta safety を実現。
- Short-lived events で behavior、snapshots で read-only state を渡し、Renderer は definition ID から static cache を参照。
- KamataEngine の既存 material ABI bug は project HLSL compatibility fix と compile-time guard で対応し、長期は engine padding fix を推奨。
- MVP では 8-bit offscreen、per-frame material、resize disabled を受容し、HDR、dedicated pipeline、texture array、
  resize reconstruction を将来の改善とする。

