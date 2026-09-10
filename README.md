# Object_Connect

`Object_Connect` は、C++20 と KamataEngine で作られた、固定解像度 1280×720 の 2D 血管接続パズル MVP です。

> **配布前に必読：[CI の強制ルール](#ci-の強制ルール)。** 正式発版は「GitHub の画面で Release を公開 → CI が添付を追加」と「tag を push → CI が Release を作成」の両方に対応します。どちらも `v1.2.3` のような 3 段の正式バージョンが必須です。annotated／lightweight tag は両方使用できます。

プレイヤーは、有効（active）になっている `root` または `follow` ノードから線を伸ばし、接続を受け入れられる `follow` または `end` ノードへ自由につなぎます。接続先はデータにあらかじめ書かれているわけではありません。ドラッグするたびに、`PuzzleBoard` がノードの接続数、残りの長さ、重複する接続、循環、`dead` ノードによる直線上の妨害を確認し、その接続が使えるかどうかを判断します。

このプロジェクトでは、ゲームジャムで素早く作れることと、新しく参加したメンバーにも読みやすいことを大切にしています。コアのゲーム処理は KamataEngine に依存しておらず、ECS、シーングラフ、完全な物理エンジンは使っていません。血管はスプライトシートのアニメーションではなく、Verlet 法の粒子チェーンと三角形ストリップを使って、その場で作っています。

## この README で使う言葉

| この文書での呼び方 | コード上の名前 | 意味 |
| --- | --- | --- |
| 有効 | active | 操作や接続に使える状態です。 |
| 接続元 | source | 線を出すノードです。 |
| 接続先 | target | 線を受け入れるノードです。 |
| 仮の線 | preview | ドラッグ中で、まだ確定していない線です。 |
| 接続を確定 | commit | 仮の線を正式な接続にします。 |
| 線を戻す | retract | 接続できなかった仮の線を戻し、長さを返します。 |

## 現在のゲームの流れ

```text
メインメニュー
  ゲーム開始 -> ステージ選択 -> プレイ中
  終了                         |
                               +-- Esc またはフォーカスを失う -> 一時停止
                               |                                  再開
                               |                                  リトライ
                               |                                  ステージ選択
                               |                                  メインメニュー
                               |                                  ゲーム終了
                               |
                               +-- 配置済みのすべての end が有効 -> クリア
                                                                    次のステージ（有効な next_level_id）
                                                                    ステージ選択
                                                                    リトライ
                               |
                               +-- next_level_id = final_results -> 最終結果
                                                                        ステージ選択
                                                                        メインメニュー
```

レベル選択の並び順は、`levels.csv` の行の順番と同じです。画面は 5 列×2 行の 10 レベル単位で表示し、左右の矢印またはマウスホイールでページを切り替えます。`次のステージ` は行の順番には依存しません。現在のレベルにある `next_level_id` を読み、catalog の中から同じ ID を探します。この項目が空、または ID が見つからない場合は、この項目を表示しません。予約値 `final_results` の場合は通常のクリアメニューを経由せず、今回のプレイでレベル選択から始めて実際にクリアしたステージだけを、通過した順番の結果カードとして最終結果に表示します。

各結果カードの集計対象は、そのステージに配置位置がある `root`、`follow`、`end` です。runtime で `active` になった数を C、対象ノードの総数を T として `【臓器 C/T】` と表示します。`dead` と、`tile_x`／`tile_y` がなく画面に配置されていない hidden node は集計しません。特別な heart／brain も対象に含め、カードのアイコンはノード固有の画像ではなく `assets/textures/node/organ.png` に統一します。カードの色は C/T の割合を表し、すべて接続したカードだけ金色の枠を付けます。メダルや S／A／B のような階級はありません。

最終結果は 1 ページに 10 カードを表示し、最初に開いたときは今回のプレイの最後のページを表示します。有効な左右の矢印またはマウスホイールでページを切り替えます。この記録は現在のプレイ中だけ保持し、保存ファイルには書きません。`次のステージ` ではそれまでの結果を残し、クリア済みステージの `リトライ` はそのステージの古い結果を取り除いてから再記録します。ステージ選択またはメインメニューへ戻ると今回の記録を消去します。

レベルを切り替える、レベルから出る、またはリトライすると、`PuzzleBoard` を作り直します。そのため、ノードの状態、接続、残りの長さはすべて初期状態に戻ります。通常のクリア画面は、約 0.6 秒たってから操作できます。

## 操作方法

- `W`／`↑`、`S`／`↓`：メニューの項目を上／下に移動します。
- `Enter` またはマウス左ボタン：選んだメニュー項目を決定します。
- レベル選択中の左右の矢印またはマウスホイール：10 レベル単位でページを切り替えます。
- 最終結果の左右の矢印またはマウスホイール：今回の結果カードを 10 枚単位で切り替えます。
- 光っている接続元ノードの上でマウス左ボタンを押し、そのまま接続できるノードまでドラッグして、ボタンを離します。
- `Esc`：プレイ中は一時停止します。一時停止中はゲームに戻ります。レベル選択画面ではメインメニューへ、クリア画面と最終結果ではレベル選択へ戻ります。
- 一時停止メニューの `リトライ`：現在のレベルを最初からやり直します。

ゲームの client 領域内では 32×32 のカスタムカーソルを使います。通常は開いた手、メニュー／有効なページ矢印の上では指差し、血管を掴んでいる間は握り拳になります。OS カーソルを隠すのはウィンドウにフォーカスがあり、ポインターが client 内にある間だけです。外へ出る、フォーカスを失う、またはカスタムカーソルの初期化に失敗した場合は OS カーソルへ戻ります。フォーカスを失うと、ドラッグ中の仮の線はすぐにキャンセルされ、ゲームは自動で一時停止します。もう一度フォーカスを得ても、自動では再開しません。

## ノードと動的な接続

各マップには、次の 4 種類のノードを配置できます。

| `node_type` | ゲーム中の動き |
| --- | --- |
| `root` | レベル開始時から有効です。接続元として使えます。複数配置できます。 |
| `follow` | 接続先として使えます。初めて接続されると有効になり、その後は接続元としても使えます。 |
| `end` | 接続先として使えますが、接続元にはできません。配置済みのすべての `end` が有効になるとクリアです。 |
| `dead` | 接続できません。現在は、直線上の接続を妨げる長方形の障害物として扱います。 |

接続はデータにあらかじめ書きません。次の条件をすべて満たすと、有効な `root` または `follow` から新しい接続を始められます。

- 接続元の `max_outgoing` に空きがある。
- 接続元の `max_outgoing_length` に残りがある。
- レベル全体の `total_length` に残りがある。
- 接続先に配置位置があり、種類が `follow` または `end` で、`max_incoming` に空きがある。
- 自分自身への接続ではなく、同じ向きの接続がまだなく、確定済みの接続に循環を作らない。
- 接続元の中心から接続先の中心までの直線が、広げた `dead` の AABB を通らない。

この仕組みにより、ゲーム中の接続グラフは、接続数の設定に応じて分岐や合流ができます。一度使った接続元も、接続数と長さが残っていれば、もう一度使えます。

マップで `tile_x`／`tile_y` が設定されていないノードは、データとしては残りますが、画面には表示されません。また、クリック判定、障害物、クリア判定にも使われません。

## 2 つの長さ制限

すべての接続は、レベル全体で 1 つの総延長を共有します。

```text
確定済みのすべての接続の長さ
+ 現在の preview が確保している長さ
+ レベル全体の残りの長さ
= total_length
```

接続元として使える各ノードには、それぞれ outgoing の長さ上限もあります。

```text
その接続元から確定した outgoing の長さ
+ その接続元から仮の線を伸ばしている場合、仮の線が確保している長さ
+ その接続元の outgoing の残りの長さ
= max_outgoing_length
```

1 回の preview で実際に使える長さの上限は、次の値です。

```text
min(レベル全体の残りの長さ, 接続元の outgoing の残りの長さ)
```

ドラッグに必要な長さは `distance(source, cursor) * minimum_slack_ratio` です。一度確保した長さは増えるだけで、カーソルを戻しても自動では短くなりません。そのため、線にたるみを持たせられます。ただし、その分だけ後で使える長さは減ります。ノードのない場所、接続できないノード、または `dead` に妨げられた場所でボタンを離すと、仮の線は約 0.22 秒かけて戻り、確保した長さも少しずつ返されます。

HUD には `残り n / total` を表示します。通常の操作状態で、接続数に空きのある接続先が残っているのに、どの接続にもレベル全体または接続元の長さが足りない場合は、`長さが足りません` を表示します。`wrap_edges=1` のレベルでは、直接経路だけでなく上下左右および角を越える隣接 image の実経路長と障害物も同じ判定に含めます。

## `dead` 障害物で現在できること

`dead` ノードは、`tile_x`／`tile_y`、`width_tiles`、`height_tiles` を使って長方形の AABB を作ります。Board は、血管の最大幅の半分だけ AABB を広げます。ドラッグ中に接続元の中心からカーソルへ向かう線が `dead` に先に当たると、仮の線の先端は最初の接触位置の少し手前で止まります。接続を確定するときも、接続元の中心から接続先の中心までの線をもう一度調べます。境界に触れた場合も、妨げられたものとして扱います。

現在は、先端を止める処理と、確定時の直線判定まで実装しています。

- 仮の線の先端は `dead` を直接通り抜けられません。ただし、血管の途中にある Verlet 粒子と確定済みの血管は、`dead` と衝突したときの押し戻しを行いません。
- 血管は自動で道を探したり、障害物に沿って曲がったりしません。
- 描画処理は、血管を描いた後に `dead` の画像または代わりの長方形を描き、見た目上の一部の重なりを隠します。

`dead` を回り込む道を作りたい場合は、途中に `follow` ノードを置いて中継点として使います。

`dead` は画像を使わず、骨灰色の面と濃い輪郭、ID と tile 座標から決まる灰白色／濃灰色の斑点で描画します。斑点はアニメーションせず、見た目を変えても位置、幅、高さ、AABB、clearance、阻害判定は一切変わりません。

将来、本当に骨をよける動き、粒子の衝突、経路探索を追加する場合は、別の計算処理を設計する必要があります。これらはまだ実装していません。

## 3 層の CSV データ

元のデータは `NoviceResources/data/` にあり、ビルド後に `Resources/data/` へコピーされます。

```text
data/
  levels.csv             レベル順、マップのパス、次のレベルID、全体予算、見た目
  nodes.csv              再利用できるノードのひな形一覧
  maps/
    stage_01.csv         各レベルで実際に使うノードと配置位置
    ...
    stage_20.csv
    archive/             catalog から参照しない試作マップ
```

### `levels.csv`

```text
level_id,level_name,map_path,next_level_id,total_length,minimum_slack_ratio,background_color,vessel_color,base_width,tip_width,width_variation,wrap_edges
```

- CSV の行の順番が、Level Select の並び順になります。
- 同梱レベルの `level_id` は `stage_01`～`stage_20`、`level_name` は `stage 01`～`stage 20` に統一します。どちらも小文字の英語と数字だけを使います。
- マップ名は level ID と一致させ、`stage_01` なら `data/maps/stage_01.csv` を使います。
- `map_path` は `Resources/` からの安全な相対パスです。
- `next_level_id` は空にできます。ゲームは、指定した ID が本当に存在するときだけ Next を表示します。予約値 `final_results` を指定すると、そのステージのクリア直後に最終結果へ移動します。この値は `level_id` には使用できません。
- `total_length` は、レベル全体で使える長さです。
- `minimum_slack_ratio` が空の場合は、初期値 1.05 を使います。
- 色は `#RRGGBB` または `#RRGGBBAA` で指定します。
- 背景、血管、幅に関する項目が空の場合は、読み込み処理の初期値を使います。
- `wrap_edges` は `1` で上下左右の境界接続を有効にし、`0` または空欄で無効にします。旧形式のように列自体がない場合も無効です。
- その他の値は起動時に警告を一度表示し、安全側として無効にします。
- 読み込みは上記の現行 12 列形式と、末尾の `wrap_edges` がない従来の 11 列形式に対応します。

### `nodes.csv` のひな形一覧

```text
preset_id,node_type,texture_path,width_tiles,height_tiles,display_name,max_incoming,max_outgoing,max_outgoing_length
```

`NodePresetCatalogLoader` を使うと、このファイルだけを読み込めます。ゲーム開始時に使う `PuzzleCatalogLoader` もこのファイルを読み、マップ上のノードの初期値として使います。

同梱する `root`／`follow`／`end` はすべて 3×3 tiles、つまり 48×48 論理ピクセルです。heart、lung、liver、kidney、brain、stomach と generic organ の 48×48 RGBA 素材を使います。loader 自体は引き続き別サイズを扱えます。`dead` の既存サイズは変更しません。

マップの行に `source_preset_id` がある場合、読み込み処理は最初にそのひな形をコピーし、その後、マップで「空ではない」項目を上書きします。よく使う器官は `nodes.csv` で一度だけ管理しながら、レベルごとにサイズ、名前、画像、接続数を変えられます。

### レベルごとのマップ CSV

```text
instance_id,source_preset_id,node_type,texture_path,width_tiles,height_tiles,display_name,tile_x,tile_y,max_incoming,max_outgoing,max_outgoing_length
```

- `instance_id` は、そのマップの中で重複しない ID にします。
- 一般マップの instance ID は、root を `heart`、end を `brain`、follow を行順の `organ_01`～、dead を行順の `bone_01`～とします。意味のある固有名を持つマップでは、`lung` や `rib_cage` のような小文字英語の lower_snake_case を使えます。
- `display_name` は画面表示用の UTF-8 文字列です。同梱マップの `organ_follow` は、貼り絵に合わせて `肺`、`肝臓`、`腎臓`、`胃` のいずれかを使います。心臓と大脳は、それぞれ特別な root／end ノード専用です。
- `source_preset_id` は空にできます。指定する場合は、`nodes.csv` に同じ ID のひな形が必要です。
- `node_type` に使える値は `root`、`follow`、`end`、`dead` だけです。ひな形がある場合は空にして引き継げます。ひな形がない場合は必ず指定します。
- 1 マスは固定で 16×16 の論理ピクセルです。`tile_x`／`tile_y` は長方形の左上の位置です。
- `width_tiles`／`height_tiles` でノードの AABB の大きさを決めます。
- `tile_x` と `tile_y` は、両方を指定するか、両方を空にしてください。
- `width_tiles`、`height_tiles`、`display_name`、`texture_path`、3 つの接続数／長さ項目には、すべて同じルールがあります。ひな形がある場合、空ならひな形の値を引き継ぎ、空でなければ上書きします。ひな形がない場合、空の項目には型ごとの初期値が入ります。
- 空の項目を使って、ひな形にある `display_name` や `texture_path` を明示的に消すことはできません。名前や画像がないノードが必要な場合は、その項目が最初から空のひな形を使うか、ひな形を指定しないでください。
- 最終的な `display_name` が空の場合、UI は名前を表示しません。`texture_path` は画像ファイルのパスです。空でない場合、読み込み処理がファイルの存在を確認し、描画処理がその画像を表示します。
- `root`／`follow` を接続元として使うには、`max_outgoing` と `max_outgoing_length` の両方を 0 より大きくする必要があります。`follow`／`end` を接続先にするには、`max_incoming` を 0 より大きくする必要があります。

CSV のヘッダー名と順番は、定義と完全に同じにしてください。読み込み処理は UTF-8 BOM、LF／CRLF、ダブルクォートで囲んだ項目、項目内の改行、`""` による引用符のエスケープに対応しています。ID には lower_snake_case を使います。表示する文字には制御文字を使えません。読み込みは一度仮の場所で完了させてから反映するため、失敗しても途中までの一覧は残りません。エラーには、ファイル名、行、項目名が表示されます。

現在の読み込み処理は、項目、パス、基本的な型を中心に確認しています。画面の範囲、ノード同士の重なり、レベルを本当にクリアできるかどうかは、まだ事前確認していません。データ作成時は、これらが自動で確認されることを前提にしないでください。

## 現在の描画方法

`PuzzleRenderer` は、単色描画用の DirectX 12 描画処理で、背景、血管、ヒント、画像がない場合の代わりの長方形を描きます。`texture_path` があるノードは、KamataEngine のスプライトで表示します。

```text
背景
-> 血管の暗い外側 / 深い赤色の中心 / 自動生成する肉らしいピクセル
-> 骨灰色の dead と固定斑点
-> active halo と source の脈動表示
-> root / follow / end の画像または代わりの長方形
-> UTF-8 HUD / メニュー overlay
```

休止中（まだ接続されていない）のノードは 55% の明るさで表示します。有効なノードは原色と固定 halo で表示し、血管を伸ばせる接続元には追加の脈動を表示します。現在掴んでいる source は、より明るく速い脈動になります。配置位置がないノードは描きません。`display_name` が空の場合は文字を描きません。名前はノード下端から 4px 下、18px、中央揃えで、貼り絵と重ならない位置に描画します。

レベルに入るとき、描画処理は最終的な `texture_path` を読み込み、ノードのスプライトを作ります。そして、`width_tiles × height_tiles` と配置位置に合わせて表示します。同じレベル内で同じパスを使う場合は、画像のハンドルを共有します。レベルを切り替えるときは、両方のレベルで使うハンドルをそのまま利用し、古いレベルだけで使っていたハンドルを解放します。そのため、毎フレームの描画で同じ画像を読み直すことはなく、以前のレベルの画像がディスクリプターを使い続けることもありません。

UI とレベルの描画処理は、同じ参照カウント付きの一覧を通して画像のハンドルを共有し、片方が早く解放しないようにしています。配置済みノードが使う `texture_path` の種類は、1 レベルにつき最大 255 個です。これにより、512 パス分の一覧の中で、古いレベルと新しいレベルを切り替え中に同時に持つことができ、失敗した場合も元のレベルをそのまま残せます。パスが空の場合だけ、ノードの種類ごとの色を使った代わりの長方形を表示します。`dead` の画像と代わりの長方形は、どちらも血管より手前に描きます。

## Unicode フォントと文字表示

HUD とメニューの文字は KamataEngine の固定 ASCII `DebugText` ではなく、runtime の `FontSystem` が描画します。入力は UTF-8 として厳密にデコードされ、Unicode scalar value、glyph layout、glyph cache、`R8_UNORM` atlas、文字に依存しない textured quad、DirectX 12 draw call の順に処理されます。ASCII、日本語、改行を同じ API で扱い、幅と baseline は TTF の advance、bearing、kerning、ascent、descent、line gap から計算します。画面上のゲーム title `OBJECT CONNECT` は維持し、メニュー見出し、項目、操作案内と HUD は日本語で表示します。

同梱フォントは `NoviceResources/fonts/BIZUDPGothic-Regular.ttf` です。ビルド後の必須 runtime asset は `Resources/fonts/BIZUDPGothic-Regular.ttf` で、`GameConfig::uiFontPath` の初期値から解決されます。CMake は configure 時にファイルの存在を検査します。有効な TTF として読み込めない場合は、解決後のフルパスを含むエラーでゲームの初期化に失敗します。この TTF はゲームに同梱した信頼できるファイルだけを使用してください。FontSystem は KamataEngine に付属する `imstb_truetype.h` 1.26 を private 実装として使い、FreeType、SDL_ttf、OS のシステムフォント、追加 DLL には依存しません。

Glyph は `{font, pixel size, code point}` ごとに初回だけ rasterize し、1px の透明 padding を付けて 1024×1024 の atlas page へ順に格納します。最初の page は font load 時に GPU resource failure を検出するため作成し、満杯になった後の page は必要時にだけ増やします。既存の領域は移動も上書きもしません。同じフォントパスを複数回ロードした場合は参照カウント付きの font record を共有し、最後の unload で atlas、upload buffer、descriptor と glyph cache をまとめて解放します。layout は 256 件の LRU cache を使い、1 frame に queue できる glyph は最大 4096 個です。欠けている文字は U+FFFD、さらに存在しなければ glyph 0 へ安全に置き換えます。

現在は単純な左から右への glyph 配置です。複雑な shaping、fallback font chain、IME、縦書き、ルビ、rich text は実装していません。FontSystem が Unicode を描画できることと、翻訳や locale 切替を備えた localization system があることは別です。

## 付属のレベルデータ

- `FIRST LINK`：Heart と Brain を使い、最小構成の root → end 接続を確認します。
- `AROUND BLOCK`：Heart、Lung、Liver、Brain と、長方形の dead エリアがあります。
- `CLOT PATH`：複数の follow 器官、Brain、2 つの dead エリアがあり、複数 source、接続数、長さの使い方を確認できます。

これらのマップに接続はあらかじめ設定されていません。実際の経路は、すべてプレイヤーがゲーム中に決めます。

## 音声素材とミックス

runtime の `GameAudio` は `Resources/audio/` の `bgm_start.wav`、`bgm_loop.wav`、
`line_hold.wav`、`line_relax.wav` を初期化時に一度だけ読み込みます。起動直後に intro を
0.9 で一度再生し、終了を polling した次の update で 0.4 の loop へ移ります。実際に一度
レベルへ入った後でメインメニューに戻ったときだけ、この sequence を intro から再開します。
ステージ選択を見て戻るだけでは再開しません。個別 clip の読み込みに失敗した場合は、
毎 frame 再試行せず安全に無音へ fallback します。

有効な source からドラッグを始めると `line_hold` を 0.65 の one-shot で一度再生し、BGM を
50ms で基準音量の 40% へ下げます。実際の mouse release では hold を 50ms で止め、
`line_relax` を 2.0 で一度再生して BGM を 250ms で戻します。Esc、失焦、Retry、切り替えなどの
強制 cancel は hold を止めますが relax は鳴らしません。

## ビルドと実行

Windows x64、Visual Studio 2026 の C++ Desktop workload と Windows SDK、`Visual Studio 18 2026` generator に対応した CMake が必要です。KamataEngine SDK は `third_party/KamataEngine/` に同梱しているため、外部の `Runtime` ディレクトリ、別の依存リポジトリ、submodule、Git LFS は不要です。通常の Git clone でヘッダー、Debug／Release の library と対応する PDB を取得します。

以下はリポジトリのルートで実行します。`Build.ps1` は Visual Studio に付属する対応 CMake を優先して探します。

```powershell
.\Build.ps1
.\Build.ps1 -Configuration Debug
.\Run.ps1
.\Run.ps1 -Configuration Release -SkipBuild
```

`Build.ps1` の既定構成は配布用の Release、`Run.ps1` の既定構成は開発用の Debug です。構成を切り替えるためにスクリプトを書き換える必要はありません。`Run.ps1` は通常ビルドしてから実行し、`-SkipBuild` を付けた場合だけ既存 EXE を使います。Debug は `/MDd`、Release は `/MT` で、同梱するエンジンの構成と合わせています。Debug EXE は開発用 runtime が必要なので配布しないでください。

旧 `KAMATA_ENGINE` 環境変数は参照しません。旧 `KAMATA_ENGINE_ROOT` cache entry は configure 時に削除し、CMake は常にプロジェクト内の SDK を使います。Build／Run の `-KamataEngineRoot` 引数も廃止しています。古い CLion profile に残っている外部パスの CMake option は取り除き、CMake を reload してください。

CLion では Visual Studio の x64 toolchain と `clion-debug`／`clion-release` presets を選びます。Ninja を直接使う場合は、x64 の Visual Studio Developer PowerShell で次を実行します。

```powershell
cmake --preset clion-debug
cmake --build --preset clion-debug
ctest --test-dir build/clion-debug --output-on-failure

cmake --preset clion-release
cmake --build --preset clion-release
ctest --test-dir build/clion-release --output-on-failure
```

実行ファイルは `target/<Configuration>/Object_Connect.exe` に作られます。ビルド時に `NoviceResources/` 全体を、実行ファイルと同じ場所にある `Resources/` へ同期します。必須の `fonts/BIZUDPGothic-Regular.ttf` も含みます。プログラムの開始時に、作業フォルダーを実行ファイルのある場所へ設定します。同一 checkout で VS と Ninja を同じ構成に対して並列ビルドすると出力先が重なるので、順番に実行してください。CI の各組み合わせは別の runner で処理します。

Debug の DirectX debug layer が必要とする `dxcompiler.dll` と `dxil.dll` は、CMake が Windows SDK の x64 Redist から Debug EXE の横へコピーします。Release では配置せず、以前のビルドで残った同名の 2 ファイルもデプロイ時に削除します。純粋な Ninja Release configure はこの DXC Redist を必要としません。VS の multi-config preset は Debug を含むため、configure 時に DXC Redist が必要です。標準外の SDK を使う場合は、`OBJECT_CONNECT_DXC_REDIST_DIR` に 2 ファイルのあるディレクトリを指定します。Windows SDK 自体は Release のコンパイルにも必要です。

プロジェクトは C++20 を使い、MSVC には `/W4 /WX /sdl /permissive- /utf-8` を設定しています。

## テスト

`ctest` を含む CMake の `bin` を PATH に設定した PowerShell、または Visual Studio Developer PowerShell で実行します。各構成を先にビルドしてください。

```powershell
ctest --test-dir build/vs2026-x64 -C Debug --output-on-failure
ctest --test-dir build/vs2026-x64 -C Release --output-on-failure
```

`Object_Connect_CoreTests` はウィンドウを作らず、GPU も必要ありません。コアテストでは、CSV、最終結果の経路と器官集計、同梱 node の 3×3／texture 解決、既存 `dead` の寸法、AABB の形状判定、動的な接続元／接続先、接続数と 2 つの長さ制限、重複／循環／`dead` の直線判定、仮の線を戻したときの長さの返却、`BloodTentacle`、`RibbonStrip`、`GameFlow` と純粋な cursor 状態解決を確認します。別の fake-backend test は BGM phase、欠損 fallback、hold／release／cancel と duck fade を確認します。

文字まわりの headless tests は、ASCII と 2／3／4-byte UTF-8、日本語の混在、overlong／surrogate／範囲外／途中で切れた不正列の U+FFFD 置換、CR／LF／CRLF、複数行の baseline と alignment を確認します。さらに、production と共通の lazy-residency／atlas seam を fake work で駆動し、同じ glyph が frame をまたいで一度だけ rasterize／upload されること、font と pixel size の cache 分離、layout cache hit、LRU eviction、missing glyph、atlas 作成失敗、font ID の非再利用と安全な枯渇を検証します。CSV の日本語 `level_name`／`display_name` も round-trip の対象です。別の headless lifecycle test は未初期化／invalid handle と複数回の `Finalize` を検証します。実際にロードした font の stale handle、同じパスの参照カウント、GPU unload lifetime は runtime integration review の対象です。

GPU を使った画面、ドラッグの感触、重なり方、HUD の配置は、人の目と操作で確認する必要があります。特に `BIZUDPGothic-Regular.ttf` が必要な日本語 glyph を含むこと、`ステージ選択` が tofu にならず中央に配置されること、DirectX 12 debug layer に resource-state error や終了時の live-object leak がないことは実機で確認します。`Flush` は現在の command list に draw を記録する処理なので、最後に使用した frame の `DirectXCommon::PostDraw` が完了してから font を unload／finalize します。

## Release ZIP の作成

`Package.ps1` は既にビルドした Release 成果物を検査して ZIP にします。ビルドやテストは代行しないため、次の順で実行してください。

```powershell
.\Build.ps1 -Configuration Release
ctest --test-dir build/vs2026-x64 -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Release tests failed.' }
.\Package.ps1
```

テストが成功したことを確認してからパッケージ化します。`-Version` を省略すると EXE のリンク時に記録した commit SHA の先頭 12 桁を使います。Git がない、ソース ZIP に `.git` がない、初回 commit 前、または親ディレクトリの別 repository しか見つからない場合も通常のビルドは警告と出典 `unknown` で継続しますが、`Package.ps1` は本プロジェクトの有効な Git checkout と出典 metadata を必須とし、出典不明の成果物はパッケージ化しません。現在の HEAD がビルド後に変わっていても、成果物を新しい commit のものとして扱いません。正式タグに対応する検証済み成果物を手動で再現する場合は `-Version v1.2.3` のように明示できます。この場合、既存の tag が解決する commit、リンク時の commit、変更のない作業ツリーの HEAD が一致している必要があります。tag は annotated／lightweight のどちらでも構いません。引数だけでは Git tag や Release は作成されません。

`-InputDirectory` の既定値は `target/Release`、`-OutputDirectory` の既定値は `target/packages` です。既存の同名出力は上書きしません。再検証には `-OutputDirectory target/packages/recheck` のように新しい出力先を指定します。PowerShell 5.1 以降に対応し、`dumpbin` は Visual Studio から自動検出するため、このスクリプトのためだけに Developer PowerShell を開く必要はありません。通常のローカル作業用 package では未コミットの変更やリンク後のソース変更をビルド情報に明記して警告しますが、正式タグと CI ではソースと EXE の不一致を拒否します。

プレイヤー向けファイルは `BloodLine-windows-x64-<tagまたは短commit>.zip` です。ZIP のルートには `Object_Connect.exe`、完全な `Resources/`、適用される第三者ライセンス情報の `LICENSES/` と `build-info.json` を収録します。PDB、テスト EXE、third-party library、Debug 用 DXC DLL は収録しません。ZIP の横に `.zip.sha256` と `.build-info.json` を出力し、成果物を追跡します。シンボルはプレイヤー ZIP と分けて扱います。

スクリプトは資源の欠損を検出し、`dumpbin` で Debug CRT、動的 MSVC／OpenMP runtime、`dxcompiler.dll`／`dxil.dll` への依存を拒否します。Windows の標準 DLL（`D3DCOMPILER_47.dll` など）は除外対象ではありません。静的リンクは「Windows の DLL を一切使わない」という意味ではなく、import 検査だけで GPU や遅延ロードの動作まで保証できるわけでもありません。

## GitHub Actions と配布

### 操作ごとの役割と正式発版の入口

**GitHub の画面だけで発版できます。コマンドで tag を push する方法も引き続き使えます。** どちらの入口でも同じ正式バージョン、commit、4 構成の build・test、package 検査を通します。画面で Release を公開した直後は、検証と添付の完了を待ってください。Release が見えることだけでは、ゲーム ZIP が配布可能になったとは限りません。

| 操作 | build・test・ZIP | GitHub Release への反映 |
| --- | --- | --- |
| ローカルの `git commit`／`git tag` | 実行しない。GitHub へはまだ送信されない | しない |
| 新しい commit を `git push origin master` で送信 | push が受理された後に実行する | しない。CI は受理済み push を取り消せない |
| `master` 宛ての PR | 実行する | しない |
| Actions の `Run workflow` | 実行する | しない。実行対象に tag を選んでも正式発版にはならない |
| 小文字 `v` で始まる tag を push | 入口検査後、検証済みの完成添付がなければ実行する | Release がなければ作成する。公開済みなら安全条件を確認して不足添付だけ追加する |
| GitHub 上で正式 Release を公開（`release: published`） | 同上 | 公開した Release に不足添付だけ追加する。タイトル、説明、手動添付は保持する |
| Release を draft として保存／公開済み Release の説明だけ編集 | そのイベントでは実行しない | しない。draft は実際に公開した時点で起動する |
| prerelease として公開 | 入口検査で拒否する | prerelease は未対応 |

手動 run が成功しても、正式 tag の検査や Release 作成まで成功したことにはなりません。変更のない `git push`（`Everything up-to-date`）も、新しい run を起動しません。

**画面で新しい tag を作りながら Release を公開する操作は正常です。** 同じ tag の push と Release 公開は別イベントなので、Actions に 2 件の run が表示されることはあります。workflow 全体を tag 単位で直列化し、後から実行する `Prepare event` が遠隔 tag／commit と完成済みの 3 添付を読み取り検証します。さらに同じ workflow・tag・commit の正式 run が既に成功していることを確認し、両条件を満たす場合だけ `needs_build=false` として build・package・publish をスキップします。先行 run の成功後にもう一度完全ビルドすることはありません。Release ページや手動添付があるだけでは CI 成功の代わりにしません。通常の branch push／PR／手動 run はこの省略の対象外です。[GitHub の concurrency](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency) を使用しており、実行中の同版 run は後発イベントでキャンセルしません。

### CI の強制ルール

以下は推奨事項ではなく、**tag push と Release 公開の両方**に対してスクリプトが実際に検査して停止する条件です。各段階の条件を満たす必要があります。

| 段階 | 必須条件 | 違反した場合 |
| --- | --- | --- |
| コンパイル前：`Prepare event` | tag 名は小文字 `v` + 数値 3 段の `vMAJOR.MINOR.PATCH`、全体 80 文字以内。各数値は `0` または先頭が `0` 以外。prerelease／build metadata は使わず、Release の prerelease 指定も不可 | 準備 job で失敗し、4 組の build と後続 job は実行しない |
| 同上 | annotated／lightweight tag のどちらでもよいが、対象は commit とする。別 tag を指す nested tag は不可。tag が解決する commit と checkout の HEAD が一致すること | 不正な対象や、tag と実際に検査するソースの不一致を拒否する |
| 同上 | tag commit が `origin/master` の履歴に含まれること。現在の先頭 commit と同じである必要はない | 未統合の feature branch の commit は発版できない |
| コンパイル前の重複検査 | 完成済みの遠隔 ZIP、`.zip.sha256`、`.build-info.json` の version／commit、相互の hash と ZIP 内の metadata／資源を検証し、同じ workflow・tag・commit の正式 run の成功履歴も確認する | 両方が正しければ後続の build・package・publish を省略。添付不足や成功履歴なしでは通常の全検証を実行。不整合や API エラーは停止する |
| build・test（新規／未完了版） | VS2026／Ninja × Debug／Release の 4 組すべてで build とテストが成功すること | package と release job はスキップする |
| package | Release x64、既知の link-time commit、EXE の SHA-256、ソースと配布資源の集合／SHA-256 が一致すること。CI の build 元と package 元は同一 commit で、双方に未コミット変更がないこと | ZIP を正式成果物にしない。正式版は tag の commit も一致が必要 |
| package | 必須資源・ライセンスが揃い、禁止する DLL 依存や Debug sidecar、資源内の開発用 binary がないこと。同名の ZIP／チェックサム／build-info を上書きしないこと | package を停止する。DLL 条件は [Release ZIP の作成](#release-zip-の作成) を参照 |
| 公開直前 | 遠隔 tag が現在も検証済み commit に解決すること。添付 ZIP の SHA-256 と build metadata の version／commit が一致すること | tag の移動や添付の不一致を検出したら発版を停止する |
| 公開直前 | 既存 Release を使う場合、同じ tag の公開済み正式 Release であること。Release 公開イベントでは ID もイベント時と同じこと。draft／prerelease を CI が勝手に正式公開しない | 既存 draft／prerelease、削除・作り直しされたイベント元 Release は拒否する。公開済みのタイトル、説明、手動添付は変更しない |
| 既存添付の照合 | 管理対象は ZIP、`.zip.sha256`、`.build-info.json` の 3 ファイル。3 つ揃っている場合、遠隔の 3 ファイルを取得して相互の hash、version、commit を検証する | 正しければ再アップロードせず成功。不整合なら停止する |
| 不足添付の追加 | 3 ファイルの一部だけがある場合、既存分は今回の候補ファイルと byte 単位で一致すること。その上で不足分のみ追加する | 同名の中身が異なる場合は停止する。削除、`--clobber`、上書きはしない |
| 集約結果：`CI validation` | 通常 CI では準備、4 組の build、package がすべて成功すること。正式版の重複省略だけは、準備の完成添付／過去 CI 検証に成功し、build／package が意図どおり skipped であること | 依存 job の失敗・キャンセルや、通常 CI の想定外の skipped を成功にしない。publish もこの成功を必須とする |

`git tag v1.1.0` と GitHub の画面で作った lightweight tag は、種類を理由に拒否しません。コマンド操作では記録を残せる `git tag -a` を推奨しますが強制ではありません。一方、`git tag -a v1.1 -m "Release v1.1"` は annotated でも名前の段数が不足するため拒否します。

| tag 名の例 | 判定 |
| --- | --- |
| `v0.1.0`、`v1.1.0`、`v1.2.3` | 名前の形式は合格。ただし他の強制条件も必要 |
| `v1.1`、`v1` | 段数不足で拒否。`v1.1` を `v1.1.0` と自動解釈しない |
| `v01.1.0`、`v1.01.0` | 不要な先頭ゼロで拒否 |
| `v1.1.0-rc.1`、`v1.1.0+build.1` | 接尾辞に未対応のため拒否 |
| `1.1.0`、`V1.1.0` | tag push の入口には一致しない。Release 公開で起動した場合も名前の検査で拒否 |

検査の実装は [ReleasePolicy.ps1](scripts/ReleasePolicy.ps1)、[Assert-ReleaseTag.ps1](scripts/Assert-ReleaseTag.ps1)、[ReleaseBuildSupport.ps1](scripts/ReleaseBuildSupport.ps1)、[Package.ps1](Package.ps1)、[ReleasePublishSupport.ps1](scripts/ReleasePublishSupport.ps1)、[windows-build.yml](.github/workflows/windows-build.yml) にあります。これらの条件と「以下の commit 命名の推奨」を混同しないでください。

### 実行と成果物の取得

`Windows build and release` workflow は `master` への push、`master` 宛ての pull request、Actions 画面からの手動実行、`v*` tag の push、Release の `published` で動きます。PR は通常の `pull_request` として実行し、リリース用の書き込み権限を渡しません。

`Prepare event` で入口と出典、正式版の既存添付を確認し、package／発版ポリシー／公開処理のテストを遠隔への書き込みなしで実行します。通常 CI と未完了の正式版では、その後 `windows-2025-vs2026` 上で VS2026／Ninja × Debug／Release の 4 組を独立に configure・build し、各組で 3 件の CTest suite と package 検査用スクリプトのテストを実行します。`/W4 /WX` は解除しません。4 組すべて成功してから VS2026 Release を共通の `Package.ps1` でパッケージ化します。`CI validation` は依存 job の失敗時にも結果を集約する固定名の check です。Ninja Release は互換性検証用で、別のプレイヤー ZIP は作りません。ビルド cache は使用せず、実際の MSVC、CMake、Windows SDK バージョンを記録します。runner 名の固定はコンパイラの更新停止を意味しません。

GitHub の **Actions → 対象 run → Artifacts** から成果物を取得します。`player-package-<sha>` がプレイヤー用 ZIP と SHA-256 チェックサム、`symbols-<generator>-<config>-<sha>` がシンボル、`logs-<generator>-<config>-<sha>` が build ログ、`logs-prepare-<sha>` が入口検証ログです。GitHub が artifact 全体を ZIP に包んでダウンロードする場合は、その内側にある `BloodLine-windows-x64-...zip` が配布用です。これらの保存期間は 30 日です。job 間転送専用の `release-input-<sha>` は 1 日だけ保持し、配布には使いません。失敗した run はログを確認し、途中の EXE を正式成果物として配布しません。

完成済み版を検証してスキップした run には、新しい `player-package` や build ログはありません。準備ログと `CI validation` の結果を確認し、既存の GitHub Release から配布 ZIP を取得します。2 件目の run の build が skipped であることは、この場合だけ正常です。

同じ Actions run の job を再実行するときは、同名の **CI artifact だけ**を `overwrite: true` で置き換えます。これは artifact 名の重複による 409 を避けるためで、**GitHub Release の 3 添付を上書きしてよいという意味ではありません**。Release 添付の照合・不足分のみ追加という規則は変わりません。

正式発版の run は、4 組の検証と package が成功した後、同じ tag の GitHub Release を作成または検証し、ZIP、チェックサム、ビルド情報の 3 ファイルを揃えます。新規作成時だけリリースノートを自動生成し、既存 Release のタイトル、説明や手動添付には触れません。通常の branch push／PR／手動 run は Release を作りません。Release の書き込み権限は正式発版の publish job だけが持ち、追加の依存ダウンロード token は不要です。

### commit の規約

**この節は推奨規約であり、CI による強制検査ではありません。** commit メッセージの形式違反だけを理由に build や発版が失敗する仕組みはありません。一方、上の tag／package／Release 条件は強制です。

新しい commit の推奨形式は `<type>(<scope>): <description>` です。scope は省略可能、type／scope は小文字英語、説明は日本語にします。type は `feat`、`fix`、`docs`、`refactor`、`test`、`build`、`ci`、`chore` から選びます。

```text
feat(results): ステージごとの臓器達成率を表示
fix(data): stage_05 の臓器配置を修正
build(deps): KamataEngine をプロジェクト内に同梱
ci: Release ZIP の依存検査を追加
docs: タグによるリリース手順を追記
```

一つの commit は説明できる一つの目的にまとめます。既存の `update:`／`hotfix:` 履歴は書き換えず、この規約のための commit lint も追加しません。commit の type からバージョンを自動更新する仕組みはありません。

### バージョンと正式タグ

正式バージョンは `vMAJOR.MINOR.PATCH` とします。互換性のない変更は MAJOR、新機能は MINOR、修正は PATCH を上げます。最初の実際のバージョンと変更内容の分類は維持管理者が決定します。今回は prerelease（`-rc.1` など）を扱わず、各数値の不要な先頭ゼロも使いません。`v*` に一致しても正式形式でないタグは CI が拒否します。

発版担当者は次の運用ルールも必ず守ってください。これらはチームの手順であり、すべてが CI で自動検査されるわけではありません。

- `master` に統合し、発版対象 commit の通常 CI 成功を先に確認する。新規版の正式 CI は自分の 4 組を実行するが、過去の `master` run の成功履歴までは照合しない。完成済み版の重複イベントは上記の検証後にスキップする。
- コマンドから発版するときはローカル作業ツリーを clean にしてから tag を作る。未コミット変更は push に含まれず、CI が開発者の PC の状態を検出することはできない。
- 未使用のバージョンを決め、GitHub 画面では対象を `master` にする。コマンドでは対象 commit に tag を付ける（nested tag の禁止自体は上表の CI 強制条件）。annotated tag は記録のための推奨であり、lightweight も使用可能。
- 下記の画面操作か tag push のどちらかで起動し、CI の完了と添付を確認する。コマンドでは対象 tag だけを push し、`git push --tags` は使わない。
- 公開済み tag を移動・削除・force push しない。コード修正は新しい commit と新しいバージョンで出す。
- 配布 ZIP 全体を展開し、開発環境のない Windows 機で起動・画面・音声を確認する。CI 成功だけでは代替しない。

### リリースフロー

普段の変更は **作業 branch に commit／push → `master` 宛ての PR → `CI validation` 成功後に merge → `master` の CI 確認 → 下記 A または B で発版** の順に進めます。作業 branch への push だけでは、この workflow の branch 条件に一致しません。PR を作成すると検証が始まります。`git commit` は変更の記録、`git tag` は版の目印の作成、`git push` はそれらを GitHub に送る操作であり、同じものではありません。

#### 事前設定：未検証の変更を `master` に入れない

**Actions は push 後に動くため、CI ファイルだけでは「失敗する commit の直接 push を拒否する」保証はできません。** その保証が必要な本プロジェクトでは、管理者が **Settings → Branches → Branch protection rule** で `master` に次を設定することを必須とします。この文書／workflow の変更だけでは遠隔設定は有効になりません。

- **Require a pull request before merging** を有効にし、直接 push を禁止する運用にします。
- **Require status checks to pass before merging** で、GitHub Actions が出す **`CI validation`** を必須にします。初回 run 後に選択し、可能な場合は期待する発行元を GitHub Actions に固定します。
- **Require branches to be up to date before merging** と **Do not allow bypassing the above settings** を有効にします。管理者も例外にせず、force push／branch 削除は許可しません。

保護設定後の通常変更は PR 経由です。`git push origin master` は正式発版コマンドではなく、保護条件を満たさない直接 push は拒否されるのが正常です。これにより merge 前の build・test・package を必須にできますが、テストに含まれないゲーム挙動や全 PC での動作まで保証するものではありません。[GitHub の protected branches](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches) を参照してください。

#### 方法 A：GitHub の画面で Release を公開する

1. 発版したい変更を上記の PR 手順で `master` に統合し、その commit の通常 CI が成功したことを確認します。
2. GitHub の **Releases → Draft a new release** を開きます。
3. **Choose a tag** で未使用の `v1.2.3` のような名前を入力し、新しい tag を作成します。**Target は `master`** にします。既存 tag を選ぶ場合は、その commit に今回使う workflow が含まれることを確認します。
4. タイトルと説明を入力します。**Set as a pre-release は選択しません。** ZIP を手作業で用意する必要はありません。
5. **Publish release** を押します。draft 保存だけでは起動しません。
6. **Actions → Windows build and release** を確認します。tag push と Release 公開の 2 件が見えても正常です。同じ版は直列処理され、先行 run の 4 組の build、package、publish が成功すると Assets に 3 ファイルが追加されます。後続 run は完成添付を検証して build 以降をスキップします。

GitHub 自動生成の `Source code (zip)`／`Source code (tar.gz)` はゲームの実行パッケージではありません。配布には `BloodLine-windows-x64-v1.2.3.zip` を使います。画面で作った lightweight tag を annotated tag に作り直す必要はありません。

新規 tag と Release を同じ画面で作るのは [GitHub 公式の公開手順](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository#creating-a-release) です。ただし、現在の自動化は「公開後に添付を追加」するため、repository の **Immutable releases** オプションを前提にしていません。このオプションを有効にすると公開後の添付追加は禁止されます。有効な場合は勝手に解除せず、先に draft へ全添付を揃えてから公開する別設計が必要です。ここでの「tag／添付を上書きしない運用」と GitHub の同名オプションは区別してください。

#### 方法 B：コマンドで tag を push する

次は PowerShell 用の手動手順です。`v1.2.3` は例なので、ローカル／遠隔 tag と GitHub Releases のどちらにも存在しない、決定済みのバージョンに置き換えます。以下のコマンドが失敗した場合は、その場で停止して原因を確認してください。

```powershell
$releaseWorkingTree = @(git status --porcelain)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the working tree.' }
if ($releaseWorkingTree.Count -ne 0) { throw 'Commit or preserve local changes before releasing.' }
git switch master
if ($LASTEXITCODE -ne 0) { throw 'Cannot switch to master.' }
git pull --ff-only origin master
if ($LASTEXITCODE -ne 0) { throw 'Cannot synchronize master.' }
git log -1 --oneline
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the release commit.' }
```

作業ツリーの clean と同期を確認した上で、GitHub 上で表示された commit の `CI validation` が成功していることを確認してから、次へ進みます。

```powershell
$releaseVersion = 'v1.2.3'
git tag -a $releaseVersion -m "Release $releaseVersion" HEAD
if ($LASTEXITCODE -ne 0) { throw 'Tag creation failed. Do not overwrite an existing tag.' }
git cat-file -t "refs/tags/$releaseVersion"
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the tag.' }
git show --no-patch $releaseVersion
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the release commit.' }
# この例では annotated tag を使う。表示された commit が発版対象と一致することを確認する。
git push origin "refs/tags/$releaseVersion"
if ($LASTEXITCODE -ne 0) { throw 'Tag push failed. Do not force push.' }
```

`git push --tags` は使わず、意図した一つのタグだけを送信します。Release がまだなければ、CI が作成して添付します。同じ tag の公開済み Release がある場合は、その内容を維持して添付を照合します。`git tag -a` はこの方法での推奨であり、lightweight tag も強制検査に合格できます。

#### 両方法共通：配布の確認と既存添付

正式発版 run が成功して GitHub の **Releases** に同じバージョンの ZIP、`.zip.sha256`、`.build-info.json` が揃ったことを確認します。ダウンロードした ZIP の SHA-256 をチェックサムの値と比較し、ZIP 全体を展開してください。

```powershell
Get-FileHash -Algorithm SHA256 .\BloodLine-windows-x64-v1.2.3.zip
```

公開済みタグを削除・移動・force push してはいけません。公開後にコード修正が必要なら新しいバージョンを作ります。既存 Release そのものはエラーではありません。管理対象の 3 添付の version／commit／hash が正しく、同じ正式版の CI 成功履歴も確認できれば、準備段階で再ビルド不要と判断します。添付が揃っていても成功履歴がなければ全検証を実行します。

一部の管理添付だけが既にある場合は、今回の候補と既存分が byte 単位で同じときだけ不足分を追加します。同じ commit を再ビルドしても時刻や toolchain の違いで ZIP が同一にならない場合があり、そのときは安全に停止します。手動添付は保持しますが、上記 3 ファイルと同じ名前で別の内容をアップロードしないでください。衝突を解消するために CI が自動削除・上書きすることはありません。

#### 旧 workflow からの移行

**最初に、この変更を `master` に統合して通常 CI を通してください。その後、この workflow を含む commit から新しいバージョンの Release または tag を作ります。** 古い tag は古い workflow とスクリプトを参照するため、以前の run で **Re-run jobs** を押しても新ルールへ切り替わりません。旧 run の annotated 必須／既存 Release 拒否や、2 回とも完全ビルドする動作は、その run 当時の仕様です。既存 tag を移動したり Release を削除したりせず、新バージョンで移行してください。

### 失敗時の確認

- 同じ版に 2 件の run がある：イベント名を確認する。1 件目が進行中なら 2 件目の待機は正常。完成後の重複 run は準備検証と `CI validation` が成功し、build／package／publish が skipped になる。先行 run の失敗、添付不足、正式 CI 成功履歴なしではスキップ成功にしない。
- 入口検査で失敗して build がスキップされる：最初の準備 job の失敗 step と診断ログを確認する。ログは入口検査より先に用意するため、バージョンや commit の不一致を build 失敗と区別できる。checkout 自体の失敗など、ログ開始前の問題は Actions の step 表示を読む。
- `Official versions must use vMAJOR.MINOR.PATCH`：`v1.1` のような形式を確認する。再実行だけでは直らない。
- `A release requires an annotated tag`／`This release already exists`：旧 workflow の run かを確認する。新 workflow は lightweight と公開済み Release に対応している。古い run の再実行では切り替わらないので、上の移行手順に従う。
- `Checkout does not match`／`must already belong to origin/master`：tag の対象 commit と統合状態を確認する。ビルド設定や DLL を変更して回避しない。
- draft／prerelease の拒否：正式 Release を実際に公開する必要がある。draft 保存や編集だけで run は起動しない。prerelease を今回の正式フローで発版しない。
- configure／link 失敗：該当 matrix のログで SDK、CMake、compiler バージョンと欠損パスを確認します。外部 `Runtime` のパスを戻して回避しません。
- テスト／package 失敗：原因を修正して通常の CI を通します。依存チェックを外す、Debug EXE を配布する、DLL を手作業で寄せ集める対応はしません。
- 一時的な runner／ネットワーク障害：tag とソースが変わっていなければ同じ正式 run を再実行できる。**publish だけ失敗した場合は `Re-run failed jobs` を優先し、元の `player-package` を使う。** `Re-run all jobs` は再ビルドによる byte 差で部分添付と衝突する可能性がある。既存の公開済み Release は保持し、全添付が整合していれば no-op、部分添付は byte 一致時だけ補う。
- 元の CI artifact が期限切れ／欠損：publish だけの再実行では復元できない。新バージョンで発版するか、全 build・test・package を再検証する。後者も既存の部分添付との一致が必須で、衝突したら新バージョンを使う。内容検査を緩めたり Release 添付を消したりしない。
- 既存添付の衝突：遠隔ファイルと今回の候補の version／commit／hash を確認する。CI は上書きしない。再実行で同じ候補が得られない場合は既存添付を消して回避せず、新バージョンで発版する。
- 他の PC で起動失敗：配布 ZIP 全体を展開したか、Release か、実行環境と表示された DLL 名を確認します。CI runner は開発環境なので、開発ツールを入れていない Windows 機での起動・画面・音声確認は別途必要です。

CI の導入だけではクラウド実行や実機検証の完了を意味しません。最初の push 後に実際の Actions 結果を確認してください。依存関係、権限、成果物の追跡設計は [アーキテクチャ文書](Docs/Architecture.md#16-依賴封裝與-ci-發版) を参照してください。

## プログラムの構成

```text
include/ObjectConnect/         公開 API；object_connect 名前空間
  Core/                        Application、FrameTimer
  Data/                        CSV、PuzzleData、2 つの catalog loader
  Game/                        Game、GameConfig、GameFlow
  Geometry/                    AABB の純粋な 2D 判定
  Input/                       キーボード、マウス、フォーカスの入力状態
  Math/                        Vec2、Color
  Puzzle/                      PuzzleBoard と描画用 Snapshot
  Rendering/                   DirectX／KamataEngine との橋渡し
  Tentacle/                    Verlet シミュレーションと帯形状の作成
  Text/                        UTF-8 layout、TTF glyph cache、FontSystem
src/ObjectConnect/             include と同じ構成の実装
tests/                         engine に依存しない core tests
NoviceResources/data/          levels、presets、レベルごとの maps
NoviceResources/assets/        32×32 cursor と 48×48 node textures
NoviceResources/audio/         intro／loop BGM と line hold／relax WAV
NoviceResources/fonts/         必須の BIZUDPGothic-Regular.ttf
NoviceResources/shaders/       flat-color 2D shaders
third_party/KamataEngine/      同梱 SDK、出典、ファイルのハッシュとライセンス
.github/workflows/             4 構成の検証、ZIP とタグ Release の生成
Package.ps1                   ローカル／CI 共通の Release package 検査
Docs/Architecture.md           担当範囲、データの流れ、機能追加の境界
```

`NoviceResources/axis/` と `Obj*.hlsl` は、KamataEngine の起動に必要なファイルです。このゲームでは 3D gameplay を使いませんが、これらのファイルは削除しないでください。

## 新しいメンバーにおすすめの読み順

1. `NoviceResources/data/levels.csv` と `data/maps/` を見て、レベルと配置位置を確認します。
2. `PuzzleData.hpp` を見て、4 種類のノードと 16px タイル用の補助処理を確認します。
3. `PuzzleCatalogLoader.cpp` を見て、ひな形の引き継ぎ、マップの上書き、ゲーム用データの組み立て方を確認します。
4. `PuzzleBoard.hpp/.cpp` を見て、有効なノード、動的な接続の確定、2 つの長さ制限を追います。
5. `BloodTentacle.hpp/.cpp` と `RibbonStrip.hpp/.cpp` を見ます。
6. `FontSystem.hpp/.cpp` を見て、UTF-8 decode、layout、glyph／atlas cache と quad 描画の境界を確認します。
7. 最後に `Game.cpp` を見て、プレイ中の状態、`nextLevelId`、描画処理の組み立て方を確認します。

描画処理は Snapshot を読むだけです。`PuzzleBoard` の内容を書き換えないでください。新しいレベルを追加するときは、通常、`levels.csv` に 1 行追加し、新しいマップ CSV を 1 つ作るだけです。`Game.cpp` にレベル固有の内容を直接書かないでください。

## まだ実装していないもの

- Dead と Verlet 粒子の衝突、血管が障害物をよける動き、経路探索。
- データ内の形状をすべて確認する事前検査と、ひな形／マップの編集ツールまたは書き戻しツール。
- 器官のスコア、出血／血圧のカウントダウン、セーブ、アンロックの進行状況。
- Font shaping、fallback font chain、IME、翻訳／locale 切替、ホットリロード、ECS、完全な物理処理、クリーチャー制御。
