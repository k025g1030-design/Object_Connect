# Object_Connect

`Object_Connect` は、C++20 と KamataEngine で作られた、固定解像度 1280×720 の 2D 血管接続パズル MVP です。

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

フォントファイルは `NoviceResources/fonts/game.ttf` に置いてください。ビルド後の必須 runtime asset は `Resources/fonts/game.ttf` で、`GameConfig::uiFontPath` の初期値 `fonts/game.ttf` から解決されます。ファイルがない、または有効な TTF として読み込めない場合は、解決後のフルパスを含むエラーでゲームの初期化に失敗します。この TTF はゲームに同梱した信頼できるファイルだけを使用してください。FontSystem は KamataEngine に付属する `imstb_truetype.h` 1.26 を private 実装として使い、FreeType、SDL_ttf、OS のシステムフォント、追加 DLL には依存しません。

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

Windows x64、Visual Studio 2026 C++ Desktop workload、`Visual Studio 18 2026` generator に対応した CMake、KamataEngine が必要です。KamataEngine のルートは、環境変数 `KAMATA_ENGINE` で指定します。プロジェクト内にマシン固有の初期パスはありません。

```powershell
$env:KAMATA_ENGINE = "D:\path\to\KamataEngine"
.\Build.ps1 -Configuration Debug
.\Build.ps1 -Configuration Release
.\Run.ps1 -Configuration Debug
```

一時的に環境変数を上書きせず、ビルド単位で別の場所を使う場合は、次のように指定します。

```powershell
.\Build.ps1 -Configuration Debug -KamataEngineRoot "D:\your\KamataEngine"
```

実行ファイルは `target/<Configuration>/Object_Connect.exe` に作られます。ビルド時に `NoviceResources/` を、実行ファイルと同じ場所にある `Resources/` へコピーします。必須の `NoviceResources/fonts/game.ttf` も、この処理で `Resources/fonts/game.ttf` になります。プログラムの開始時に、作業フォルダーを実行ファイルのある場所へ設定します。

Debug の DirectX debug layer が必要とする `dxcompiler.dll` と `dxil.dll` は、CMake が Windows SDK の x64 Redist から Debug 実行ファイルと同じ場所へコピーします。Release は debug layer を有効にしないため、この 2 ファイルを配置せず、以前のビルドで残った副本もデプロイ時に削除します。Release の CRT、KamataEngine、DirectXTex は静的リンクされるため、配布物は `Object_Connect.exe` と `Resources/` だけです。この処理は Visual Studio と CLion／Ninja で共通です。SDK を標準外の場所に置く場合は、CMake の `OBJECT_CONNECT_DXC_REDIST_DIR` に 2 つの DLL があるディレクトリを指定してください。

プロジェクトは C++20 を使い、MSVC には `/W4 /WX /sdl /permissive- /utf-8` を設定しています。

## テスト

```powershell
ctest --test-dir build/vs2026-x64 -C Debug --output-on-failure
ctest --test-dir build/vs2026-x64 -C Release --output-on-failure
```

`Object_Connect_CoreTests` はウィンドウを作らず、GPU も必要ありません。コアテストでは、CSV、最終結果の経路と器官集計、同梱 node の 3×3／texture 解決、既存 `dead` の寸法、AABB の形状判定、動的な接続元／接続先、接続数と 2 つの長さ制限、重複／循環／`dead` の直線判定、仮の線を戻したときの長さの返却、`BloodTentacle`、`RibbonStrip`、`GameFlow` と純粋な cursor 状態解決を確認します。別の fake-backend test は BGM phase、欠損 fallback、hold／release／cancel と duck fade を確認します。

文字まわりの headless tests は、ASCII と 2／3／4-byte UTF-8、日本語の混在、overlong／surrogate／範囲外／途中で切れた不正列の U+FFFD 置換、CR／LF／CRLF、複数行の baseline と alignment を確認します。さらに、production と共通の lazy-residency／atlas seam を fake work で駆動し、同じ glyph が frame をまたいで一度だけ rasterize／upload されること、font と pixel size の cache 分離、layout cache hit、LRU eviction、missing glyph、atlas 作成失敗、font ID の非再利用と安全な枯渇を検証します。CSV の日本語 `level_name`／`display_name` も round-trip の対象です。別の headless lifecycle test は未初期化／invalid handle と複数回の `Finalize` を検証します。実際にロードした font の stale handle、同じパスの参照カウント、GPU unload lifetime は runtime integration review の対象です。

GPU を使った画面、ドラッグの感触、重なり方、HUD の配置は、人の目と操作で確認する必要があります。特に `game.ttf` が必要な日本語 glyph を含むこと、`ステージ選択` が tofu にならず中央に配置されること、DirectX 12 debug layer に resource-state error や終了時の live-object leak がないことは実機で確認します。`Flush` は現在の command list に draw を記録する処理なので、最後に使用した frame の `DirectXCommon::PostDraw` が完了してから font を unload／finalize します。

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
NoviceResources/fonts/         必須の信頼済み game.ttf
NoviceResources/shaders/       flat-color 2D shaders
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
