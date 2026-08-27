# Object_FPS

C++20 と KamataEngine で構築した 2.5D 一人称視点 FPS の MVP です。ゲームルールは 2D XZ
グリッド上で動作し、床と壁面は 3D の透視投影カメラ、深度テスト、OBJ の四角形
インスタンスを使用して描画します。

現在の MVP には、以下の機能が含まれています。

- テキストの `#`、`.`、`P`、`M`、`R`、`D` を検証済み `TileType` に変換する
- 通行可能セルと壁セルの境界に基づいて床と壁面を生成する
- キーボード／マウス対応の開始メニュー、操作説明、ポーズメニュー、`Results` シーン
- CSV catalog による敵、Atlas animation、武器、線形レベル進行のデータ駆動設定
- kill quota を満たすまで非表示／無効となる白い仮ドアと、公開 `MapSceneManager` による黒い scene fade
- WASD による水平移動
- マウスによる yaw／pitch の視点操作（pitch は XZ 移動に影響しない）
- 左クリックの中心 hitscan、白い曳光球、マガジン／予備弾、R キー reload、射撃 cooldown と recoil
- 近接型／遠距離型の動的 spawn、4 状態 AI、A* 経路探索と grid line-of-sight
- 近接 attack の直接 damage、回避可能な橙色 enemy projectile、enemy hit flash
- プレイヤー／敵の円とグリッド壁の 2D コリジョン、壁沿いの移動、swept-circle によるすり抜け防止
- 戦闘用 ray／segment と垂直 capsule の最近 hit、および swept projectile 判定
- 緑色の仮 weapon、crosshair、HP／弾薬／reload HUD
- `CampaignRunState` による room ごとの実 kill 数と、成功／死亡別 Results
- プレイヤーを向き続ける透過 Atlas の world-space billboard と、idle／move／attack／dead animation
- camera の平行移動だけを追従する 50m sky sphere
- world 3D だけに適用する brightness 1.25／gamma 2.2 の linear-space post-process

ジャンプ、階段、高低差、複数階、天井、武器交換、拾得物、reload animation と新規音声は、
現在のスコープには含まれません。

## 必要な環境

- Windows x64
- Visual Studio 2026 の「C++ によるデスクトップ開発」ワークロード
- MSVC v145 および Windows SDK
- `Visual Studio 18 2026` ジェネレーターをサポートする CMake。現在の `Build.ps1` は
  Visual Studio 付属版を優先して検索し、見つからない場合は PATH 上に互換バージョンが必要
- KamataEngine（デフォルトの場所は `D:\code\Runtime\KamataEngine`）

プロジェクトは C++20 を使用し、MSVC の `/W4 /WX /sdl /permissive-` でビルドします。

## ビルドと実行

Debug：

```powershell
.\Build.ps1 -Configuration Debug
```

Release：

```powershell
.\Build.ps1 -Configuration Release
```

KamataEngine が別の場所にある場合：

```powershell
.\Build.ps1 -Configuration Debug -KamataEngineRoot "D:\your\KamataEngine"
```

ビルドして実行：

```powershell
.\Run.ps1 -Configuration Debug
```

すでにビルド済みの場合は、再ビルドを省略できます。

```powershell
.\Run.ps1 -Configuration Debug -SkipBuild
```

実行ファイルは `target/<Configuration>/Object_FPS.exe` にあります。`Application` は起動時に
作業ディレクトリを実行ファイルのあるディレクトリへ切り替えるため、CLion、`Run.ps1`、
エクスプローラーのいずれから起動しても、同じデプロイ済みリソースを使用します。

## CLion

CLion でこのディレクトリを開き、x64 Visual Studio ツールチェーンを使用して、次のいずれかを選択します。

- `clion-debug`
- `clion-release`

どちらのプリセットも Ninja を使用します。MSVC 環境は CLion ツールチェーンから提供されます。

## 操作

- メニューの方向キーまたは `W`／`S`：項目選択
- `Enter` または左クリック：決定
- `W`／`S`：前進／後退
- `A`／`D`：左／右へ平行移動
- マウスの水平移動：Yaw
- マウスの垂直移動：Pitch
- 左クリック：射撃（初期 pistol は semi-auto）
- `R`：マガジンに空きがあり予備弾がある場合に reload
- `Esc`：プレイ中はポーズ、ポーズ中は再開、操作説明では戻る

各 room の kill quota を達成すると白いドアが表示され、再度ドアへ入ることで次 room へ進みます。
最後の room を完了すると `MISSION COMPLETE`、HP が 0 になると `GAME OVER` の独立した
`Results` シーンへ切り替わります。到達済み room ごとの実 kill 数と中央の `MAIN MENU` ボタンを表示し、
`Enter` または左クリックでメインメニューへ戻ります。
`Results` では `Esc` は何も行いません。

カーソルはプレイ中だけロックして非表示にし、メニュー、操作説明、ポーズ中は解放して表示します。
プレイ中にフォーカスを失うか最小化すると自動的にポーズし、再びフォーカスを取得しても自動再開はしません。
ASCII UI と mouse hit-test は 1280×720 の同じ座標系を使うため、ゲームウィンドウの resize は無効です。
Input 層は物理キー、マウス、フォーカスと capture 状態のみを報告し、メニューや WASD の意味は上位層が解釈します。

開始、次マップ、最終マップから `Results`、`Results`／ポーズからメインメニューへの切り替えは、
同じ公開 `MapSceneManager` を使い、標準で 0.4 秒 fade-out、0.4 秒 fade-in します。
`Results` への全黒 commit 時に active level は破棄され、`Results` は 3D level を保持しません。
切り替え中はプレイヤー、カメラ、AI、projectile、reload と cooldown を含む simulation が停止し、Esc や
メニュー入力も破棄されます。切り替え中にフォーカスを失っても animation は継続し、完了時にも
未フォーカスで遷移先が gameplay の場合だけ、その後 Paused になります。

敵は level CSV の生成額度と同時生存上限に従い、`M`／`R` marker を row-major の round-robin で再利用して
近接型から交互に動的生成されます。安全な marker がない場合は額度を保持して次 frame に再試行します。
近接型は 0.8m、遠距離型は 1.6m の textured billboard で、表示寸法と gameplay hitbox は
別々の CSV 値です。`render_width`／`render_height` は透明余白を含む共通 frame canvas 全体の world size で、
hitbox の高さや半径から再計算されません。共通 canvas を固定したまま描くため、動作ごとに alpha bounds が
大きく変わってもキャラクターの縮尺や足元 anchor は跳ねません。近接型は
A* を使って壁を迂回しながらプレイヤーへ接近します。遠距離型は
近すぎる場合は経路探索で後退し、射程と視線を確保できる場所へ移動します。敵は常にプレイヤーを
認識します。attack event は Atlas の 0-based event frame を跨いだ時に一度だけ発生します。近接はその時点で
距離と LOS を再確認するため蓄力中に回避でき、遠距離は CSV の muzzle pixel から発射時点の player capsule 中心へ
橙色 projectile を撃ちます。敵は有効 damage を受けると短時間白く flash し、lethal hit は room kill に一度だけ
記録されます。死亡 animation は 0.4 秒表示され、active count と combat collision からは即座に外れますが、
表示中は spawn marker を占有します。生存中の敵と
プレイヤーは互いに通り抜けられず、敵同士は重なれます。Paused と scene fade 中は AI、cooldown、
reload、projectile、state elapsed、billboard pose を含む level simulation 全体が停止します。

プレイヤー射撃は camera 中心から最大 50m の ray を飛ばし、wall／floor／enemy capsule の最近 hit を
即時に解決します。さらに右下 weapon の muzzle から aim point まで遮蔽を再確認し、壁越しの命中を防ぎます。
表示される白球は hit 結果を遅延させない visual tracer です。damage は
`max(1, weapon damage - enemy defense)`、enemy projectile は swept segment で wall／player capsule を判定します。

## マップ形式

元のマップは `NoviceResources/maps/mvp_map.txt` と `mvp_map_02.txt` にあり、ビルド後は
同じ相対パスで `Resources/maps/` にデプロイされます。進行順、表示名、map path、次 level ID、
敵生成額度、同時生存上限と door 解放 kill 数は `levels.csv` で定義します。空の `next_level_id` が
最終 room を示し、`GameConfig::startLevelId` が開始 level、`GameConfig::mapTransition` が fade 時間を設定します。

```text
# = 通行不可の壁
. = 通行可能な空間
P = プレイヤーのスポーン位置（必ず 1 つだけ）
M = 近接型敵のスポーン位置（0 個以上）
R = 遠距離型敵のスポーン位置（0 個以上）
D = 次マップへの出口。最終マップでは Results への出口（必ず 1 つだけ）
```

すべての行は同じ幅で、空であってはなりません。`P`、`M`、`R`、`D` は通行可能で、
旧形式の `E` を含む未定義文字は拒否されます。
列はワールドの `+X`、行はワールドの `+Z` に対応し、マップ範囲外は常に壁として扱います。

文字から enum への変換は `GridMapLoader` だけが担当し、`GridMap` は `TileType` と marker 座標だけを保持します。
ファイルの読み取りとテキスト解析は
`GridMapLoader` が担当します。Rendering 用のジオメトリと Gameplay 用のコリジョンは、どちらも同じ
`GridMap` を読み取りますが、互いにジオメトリを受け渡すことはありません。

## リソースのデプロイ

`NoviceResources/` は、意図的に維持している KamataEngine の基本リソースの格納元です。一般的な
プロジェクト慣例に合わせるためだけに、無理に `assets/` へ改名することはありません。CMake の独立した
`Object_FPS_Resources` ターゲットは、ビルドのたびにディレクトリを実行ファイルの隣にある
`Resources/` へ同期します。そのため、マップ、シェーダー、モデルだけを変更した場合でも、古いデプロイ済み
ファイルが残ることはありません。パスは引き続き KamataEngine のリソースルートおよび OBJ/MTL の
相対パス規則に準拠します。

ゲームデータの原本は次の 4 catalog です。実行時は同じ相対位置の `Resources/data/` から読み込みます。

- `NoviceResources/data/enemies.csv`：enemy ID、combat、hitbox／render size、sheet texture、共通 frame size
- `NoviceResources/data/enemy_animation_clips.csv`：enemy／state、atlas origin、frame count／timing、attack event、ranged muzzle
- `NoviceResources/data/weapons.csv`：weapon ID、damage、magazine／reserve、recoil、automatic、fire interval、reload、texture
- `NoviceResources/data/levels.csv`：level ID／name／map／next ID、敵生成額度、active limit、clear kill count

CSV reader は BOM、LF／CRLF、quoted field と quote escape を扱い、header、型、範囲、重複 ID、参照、
resource path／存在、敵 kind と線形 level chain を preflight します。失敗時は file、row、column／field を含む
診断を返します。

MVP のマップモデルは次の場所にあります。

- `NoviceResources/map_floor/`
- `NoviceResources/map_wall/`
- `NoviceResources/cube/`（`white1x1.png` を上書き適用する出口の仮モデル）

floor／wall は 4 頂点／2 三角形の単位四角形です。出口は既存 cube を扁平な門形に scale します。
敵 billboard は `map_wall.obj` を共通 quad として再利用し、2 枚の sheet texture と 33 個の frame material を
definition ID で cache します。UV は frame 境界から half texel inset し、KamataEngine の C++ material constant
buffer と同じ packed layout で atlas offset を読み取ります。透明 pixel は alpha cutoff で depth を書きません。
sky は `assets/textures/sky/sky_sphere.png` 全体を equirectangular texture として使用します。

## テスト

対応する構成のビルドを完了してから、次を実行します。

```powershell
ctest --test-dir build/vs2026-x64 -C Debug --output-on-failure
ctest --test-dir build/vs2026-x64 -C Release --output-on-failure
```

テストソースは責務ごとに `World`、`Collision`、`Rendering`、`Data`、`Gameplay`、`Game` の
headless suite に分かれています。マップの enum 解析／読み込み／エラー座標、
World の所有権／設定、マップ形状の生成、円／グリッドのコリジョン、壁沿いの移動、
動的円のすり抜け防止、ray／capsule／swept segment、白い出口 geometry、平面移動、Player の設定、
CSV catalog／animation validation、Atlas UV／loop／clamp、post-process curve、Weapon／reload／cooldown、projectile、
動的 enemy spawn／状態／A*／LOS／近接追跡／遠距離退避、attack frame／回避／muzzle／death retention、
damage／kill、`CampaignRunState`、メニュー／ポーズ／Results の状態遷移、
fade phase／opacity／commit barrier／input lock と最終マップから Results への progression、
正式な 2 map の P/M/R/D 可達性を
網羅しています。これらは GPU やウィンドウを必要としない契約テストです。

この機能追加後の構成ごとの build／CTest 結果は、上記コマンドで個別に確認してください。ここでは、
まだ完了していない構成を検証済みとは記載しません。

自動化環境では非表示の DirectX バックバッファを確実にキャプチャできないため、床／壁面／白い出口の視認性、
起動、メニュー／Results の hover/click、WASD、マウス視点、ポーズ、scene fade とカーソル capture は、引き続き手動での実行時回帰テストが
必要です。ローカルでは Ninja プリセットの構成とビルドグラフを検証済みです。この Codex Windows
実行環境では Ninja が MSVC の子プロセスを待機する際に停止するため、CLion/Ninja の完全なリンクが
成功したとはしていません。Visual Studio ジェネレーターの Debug／Release ビルドは、この問題の影響を受けません。

## プログラム構成

```text
include/RetroFPS/             公開 API
  Core/                       Application とフレームタイミングの契約
  Math/                       エンジン非依存の値型
  Input/                      生のキーボード／マウス状態と入力アダプター
  World/                      GridMap、ローダー、World、設定
  Data/                       CSV parser と enemy／weapon／level catalog
  Collision/                  XZ movement と ray／segment／capsule combat query
  Gameplay/Player/            Player、HP、設定、コントローラー
  Gameplay/Weapon/            弾薬、射撃、reload、cooldown と recoil state
  Gameplay/Enemy/             動的 spawn、enemy state、A*／LOS AI、damage／attack event
  Gameplay/Combat/            tracer と enemy projectile simulation
  Rendering/                  Camera、map、billboard、projectile、weapon／HUD、UI／fade
  Game/                       組み立て、CampaignRunState、GameFlow、MapSceneManager
src/                          非公開実装とプラットフォーム固有の詳細
tests/
  World/                      マップ読み込みと World の所有権／設定
  Collision/                  movement と combat collision
  Rendering/                  CPU によるマップ形状生成
  Gameplay/                   Player／Weapon／Enemy／Projectile の engine-independent tests
  Data/                       CSV と catalog validation
  Game/                       campaign、メニュー／ポーズ／Results／map scene transition
Docs/Architecture.md          依存関係、所有権、拡張ガイド
NoviceResources/              KamataEngine 互換の元リソース
```

利用側のプログラムは `include/RetroFPS/...` をインクルードしてください。`src/` 内のヘッダー（Win32 の
マウスキャプチャなど）は非公開実装であり、安定した API ではありません。公開型は
`fps` 名前空間にあります。

以前の MT3 授業用実装は、このプロジェクトには含まれません。過去の数学処理やコリジョン技法を参照する
必要がある場合は、[MT3](https://github.com/k025g1030-design/MT3/) を直接確認してください。Object_FPS はこのディレクトリを
リンクもインクルードもしません。
