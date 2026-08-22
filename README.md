# Object_FPS

C++20 と KamataEngine で構築した 2.5D 一人称視点 FPS の MVP です。ゲームルールは 2D XZ
グリッド上で動作し、床と壁面は 3D の透視投影カメラ、深度テスト、OBJ の四角形
インスタンスを使用して描画します。

第 2 版のリファクタリングでは、第 1 版の目に見える動作を維持しつつ、公開 API、
プラットフォーム統合、ワールドデータ、コリジョン、Gameplay、Rendering を分離しました。
責務、依存関係、ライフサイクルの詳細については、
[Docs/Architecture.md](Docs/Architecture.md) を参照してください。

現在の MVP には、以下の機能が含まれています。

- テキストファイルから `#`、`.`、`P` のグリッドマップを読み込む
- 通行可能セルと壁セルの境界に基づいて床と壁面を生成する
- WASD による水平移動
- マウスによる yaw／pitch の視点操作（pitch は XZ 移動に影響しない）
- プレイヤーを表す円とグリッド壁の 2D コリジョン、壁沿いの移動、移動の分割によるすり抜け防止

Weapon、Enemy、Raycast Combat、Damage、Enemy AI は、後続フェーズへ明示的に延期しています。
今回は、これらの機能を妨げないモジュール境界のみを維持します。HUD、ジャンプ、階段、高低差、
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

- `W`／`S`：前進／後退
- `A`／`D`：左／右へ平行移動
- マウスの水平移動：Yaw
- マウスの垂直移動：Pitch
- `Esc`：ゲームを終了

ゲームウィンドウがフォーカスを取得すると、カーソルをロックして非表示にします。フォーカスを失うか
最小化されるとカーソルを解放し、再びフォーカスを取得したときに復元します。Input 層は物理キー、
マウスの移動量、キャプチャ状態のみを報告し、WASD の Gameplay 上の意味は `PlayerController` が解釈します。

## マップ形式

元のマップは `NoviceResources/maps/mvp_map.txt` にあり、ビルド後は
`Resources/maps/mvp_map.txt` にデプロイされます。

```text
# = 通行不可の壁
. = 通行可能な空間
P = プレイヤーのスポーン位置（必ず 1 つだけ）
```

すべての行は同じ幅で、空であってはなりません。現在、`E`、`D`、その他の文字は受け付けません。
列はワールドの `+X`、行はワールドの `+Z` に対応し、マップ範囲外は常に壁として扱います。

`GridMap` は検証済みの Grid データのみを保持し、ファイルの読み取りとテキスト解析は
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

どちらも 4 頂点／2 三角形で構成され、法線と UV を持つ単位四角形です。

## テスト

対応する構成のビルドを完了してから、次を実行します。

```powershell
ctest --test-dir build/vs2026-x64 -C Debug --output-on-failure
ctest --test-dir build/vs2026-x64 -C Release --output-on-failure
```

テストソースは責務ごとに `World`、`Collision`、`Rendering.MapGeometry`、
`Gameplay.PlayerController` の各テストスイートに分かれています。マップの解析／読み込み／エラー座標、
World の所有権／設定、マップ形状の生成、円／グリッドのコリジョン、壁沿いの移動、
すり抜け防止、平面移動、Player の設定／コントローラーによる生入力の解釈を
網羅しています。これらは GPU やウィンドウを必要としない契約テストです。

2026-08-22 に Visual Studio 2026 x64 を使用して Debug と Release のフルビルドを完了しました。
両構成とも MSVC `/W4 /WX /sdl /permissive-` を通過し、CTest もそれぞれ 1/1 で
成功しています。両方の実行ファイルについて、起動、ゲームウィンドウの作成、正常終了のスモークテストも
完了しています。また、元データと Debug／Release のデプロイ済みリソースをファイル単位で比較した
SHA-256 は一致しています。

自動化環境では非表示の DirectX バックバッファを確実にキャプチャできないため、床／壁面の実際の視認性、
WASD の操作感、マウス視点操作、フォーカス／カーソルキャプチャについては、引き続き手動での実行時回帰テストが
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
  Rendering/                  Camera、マップ形状、レンダラーの契約
  Game/                       Game ファサードと構成設定
src/                          非公開実装とプラットフォーム固有の詳細
tests/
  World/                      マップ読み込みと World の所有権／設定
  Collision/                  グリッドコリジョンと平面移動
  Rendering/                  CPU によるマップ形状生成
  Gameplay/                   Player の設定／コントローラーと生入力
Docs/Architecture.md          依存関係、所有権、拡張ガイド
NoviceResources/              KamataEngine 互換の元リソース
```

利用側のプログラムは `include/RetroFPS/...` をインクルードしてください。`src/` 内のヘッダー（Win32 の
マウスキャプチャなど）は非公開実装であり、安定した API ではありません。公開型は
`fps` 名前空間にあります。

以前の MT3 授業用実装は、このプロジェクトには含まれません。過去の数学処理やコリジョン技法を参照する
必要がある場合は、`D:\code\Source\MT3_Summer` を直接確認してください。Object_FPS はこのディレクトリを
リンクもインクルードもしません。
