# Object_FPS

C++20 と KamataEngine で構築した 2.5D 一人称視点 FPS の MVP です。ゲームルールは 2D XZ
グリッド上で動作し、床と壁面は 3D の透視投影カメラ、深度テスト、OBJ の四角形
インスタンスを使用して描画します。

現在の MVP には、以下の機能が含まれています。

- テキストの `#`、`.`、`P`、`E`、`D` を検証済み `TileType` に変換する
- 通行可能セルと壁セルの境界に基づいて床と壁面を生成する
- キーボード／マウス対応の開始メニュー、操作説明、ポーズメニュー、`Results` シーン
- 順序付きの複数マップ、白い仮ドア、公開 `MapSceneManager` による黒い scene fade
- WASD による水平移動
- マウスによる yaw／pitch の視点操作（pitch は XZ 移動に影響しない）
- プレイヤーを表す円とグリッド壁の 2D コリジョン、壁沿いの移動、移動の分割によるすり抜け防止

Weapon、Enemy entity、Raycast Combat、Damage、Enemy AI は、後続フェーズへ明示的に延期しています。
`E` の Enemy spawn 座標は保存しますが、今回は敵を生成しません。HUD、ジャンプ、階段、高低差、
複数階、天井も現在のスコープには含まれません。

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
- `Esc`：プレイ中はポーズ、ポーズ中は再開、操作説明では戻る

最後のマップを完了すると、独立した `Results` シーンへ切り替わります。`Results` には中央の
`MAIN MENU` ボタンだけを表示し、`Enter` または左クリックでメインメニューへ戻ります。
`Results` では `Esc` は何も行いません。

カーソルはプレイ中だけロックして非表示にし、メニュー、操作説明、ポーズ中は解放して表示します。
プレイ中にフォーカスを失うか最小化すると自動的にポーズし、再びフォーカスを取得しても自動再開はしません。
ASCII UI と mouse hit-test は 1280×720 の同じ座標系を使うため、ゲームウィンドウの resize は無効です。
Input 層は物理キー、マウス、フォーカスと capture 状態のみを報告し、メニューや WASD の意味は上位層が解釈します。

開始、次マップ、最終マップから `Results`、`Results`／ポーズからメインメニューへの切り替えは、
同じ公開 `MapSceneManager` を使い、標準で 0.4 秒 fade-out、0.4 秒 fade-in します。
`Results` への全黒 commit 時に active level は破棄され、`Results` は 3D level を保持しません。
切り替え中はプレイヤー、カメラと将来の全 simulation が停止し、Esc や
メニュー入力も破棄されます。切り替え中にフォーカスを失っても animation は継続し、完了時にも
未フォーカスで遷移先が gameplay の場合だけ、その後 Paused になります。

## マップ形式

元のマップは `NoviceResources/maps/mvp_map.txt` と `mvp_map_02.txt` にあり、ビルド後は
同じ相対パスで `Resources/maps/` にデプロイされます。`GameConfig::mapPaths` の順序が進行順で、
`GameConfig::mapTransition` が公開 scene transition の fade 時間を設定します。

```text
# = 通行不可の壁
. = 通行可能な空間
P = プレイヤーのスポーン位置（必ず 1 つだけ）
E = 将来の敵スポーン位置（0 個以上）
D = 次マップへの出口。最終マップでは Results への出口（必ず 1 つだけ）
```

すべての行は同じ幅で、空であってはなりません。`P`、`E`、`D` は通行可能で、未定義文字は拒否されます。
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

MVP のマップモデルは次の場所にあります。

- `NoviceResources/map_floor/`
- `NoviceResources/map_wall/`
- `NoviceResources/cube/`（`white1x1.png` を上書き適用する出口の仮モデル）

floor／wall は 4 頂点／2 三角形の単位四角形です。出口は既存 cube を扁平な門形に scale します。

## テスト

対応する構成のビルドを完了してから、次を実行します。

```powershell
ctest --test-dir build/vs2026-x64 -C Debug --output-on-failure
ctest --test-dir build/vs2026-x64 -C Release --output-on-failure
```

テストソースは責務ごとに `World`、`Collision`、`Rendering.MapGeometry`、
`Gameplay.PlayerController`、`Game.Flow`、`Game.MapSceneManager`、`Game.CampaignResources` の各テストスイートに分かれています。マップの enum 解析／読み込み／エラー座標、
World の所有権／設定、マップ形状の生成、円／グリッドのコリジョン、壁沿いの移動、
すり抜け防止、白い出口 geometry、平面移動、Player の設定、メニュー／ポーズ／Results の状態遷移、
fade phase／opacity／commit barrier／input lock と最終マップから Results への progression、
正式な 2 map の P/E/D 可達性を
網羅しています。これらは GPU やウィンドウを必要としない契約テストです。

2026-08-22 に Visual Studio 2026 x64 を使用して Debug と Release のフルビルドを完了しました。
両構成とも MSVC `/W4 /WX /sdl /permissive-` を通過し、CTest もそれぞれ 1/1 で
成功しています。また、元データと Debug／Release のデプロイ済みリソースをファイル単位で比較した
SHA-256 は一致しています。

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
  Collision/                  XZ 上の円／グリッド照会
  Gameplay/Player/            Player の状態、設定、コントローラー
  Rendering/                  Camera、マップ形状、3D／ASCII UI／screen fade の契約
  Game/                       Game、構成設定、GameFlow、公開 MapSceneManager
src/                          非公開実装とプラットフォーム固有の詳細
tests/
  World/                      マップ読み込みと World の所有権／設定
  Collision/                  グリッドコリジョンと平面移動
  Rendering/                  CPU によるマップ形状生成
  Gameplay/                   Player の設定／コントローラーと生入力
  Game/                       メニュー／ポーズ／Results／Map scene transition
Docs/Architecture.md          依存関係、所有権、拡張ガイド
NoviceResources/              KamataEngine 互換の元リソース
```

利用側のプログラムは `include/RetroFPS/...` をインクルードしてください。`src/` 内のヘッダー（Win32 の
マウスキャプチャなど）は非公開実装であり、安定した API ではありません。公開型は
`fps` 名前空間にあります。

以前の MT3 授業用実装は、このプロジェクトには含まれません。過去の数学処理やコリジョン技法を参照する
必要がある場合は、`D:\code\Source\MT3_Summer` を直接確認してください。Object_FPS はこのディレクトリを
リンクもインクルードもしません。
