# Enemy Atlas Shader Constant-Buffer ABI 技術レポート

- 文書ステータス：Object_FPS 完成版 MVP の実装後分析
- 日付：2026-08-27
- 繁体字中国語版：[EnemyAtlasShaderABI.zh-TW.md](EnemyAtlasShaderABI.zh-TW.md)
- 主な関連ファイル：
  - `NoviceResources/shaders/Obj.hlsli`
  - `NoviceResources/shaders/ObjPS.hlsl`
  - `src/Rendering/EnemyBillboardRenderer.cpp`
  - KamataEngine `External/KamataEngine/include/3d/Material.h`

## 1. 結論の要約

今回 `Obj.hlsli` を変更した直接の理由は、**GPU 側の HLSL constant buffer と、CPU 側の
`KamataEngine::Material::ConstBufferData` の実メモリ配置が一致していなかったため**です。

この不一致は、敵データ CSV、敵ステートマシン、または Atlas の row 定義によるものではありません。
CPU 側では `idle / move / attack / dead` に対応する clip が正しく選択されていました。
問題は最終段階にあり、CPU が正しい Atlas UV offset を constant buffer に書き込んだ後、
shader が異なる byte offset から値を読み取ったため、GPU が別の frame／row をサンプリングしていました。

「以前から存在していたバグか」という質問への正確な回答は、次のとおりです。

- **はい。CPU／HLSL layout の不一致は以前から存在しており、KamataEngine material interface の潜在的な問題でした。**
- **ただし、この段階より前は通常は表面化しませんでした。** 既存の map、sky、一般的な OBJ material では
  `uvOffset` がほぼ常に `(0, 0, 0)` であり、ゼロを誤った位置から読んでも結果はゼロのままだったためです。
- Enemy Atlas は、非ゼロの X／Y UV offset を継続的に利用した最初の機能であり、潜在していた不一致を顕在化させました。
- Atlas の初回統合時には CPU／GPU ABI の検査がまだなく、表示上の回帰を許してしまいました。
  今回の変更は、その統合問題に対する正式な修正です。

今回プロジェクト側の shader を修正したのは、Object_FPS がコンパイル済みの KamataEngine library を
リンクしているためです。外部 engine を再ビルドせず、敵専用の D3D12 pipeline を新設しない条件では、
HLSL を既存 CPU layout に合わせる方法が、変更範囲が最小で意味的にも正しい選択でした。

## 2. 症状と、実際には問題でなかった範囲

修正前には、次の症状が確認できました。

- 生存中かつ移動中の敵が、Atlas 最下段の死亡アニメーションを表示することがある。
- `idle / move / attack / dead` と画面上のアニメーションが無関係に見える。
- frame index が変化しても、必ずしも水平方向の次のセルへ移動しない。
- 死亡 pose は通常横長かつ低いため、誤った row の表示が render size／hitbox の問題にも見える。

一方、次の処理には state の対応ミスはありませんでした。

- CSV に記録された各 state の `origin_y_px` と `frame_count`。
- `EnemyState` から `EnemyAnimationClipDefinition` を選ぶ switch。
- attack／dead の elapsed time と loop／clamp 規則。
- `render_width / render_height` と `hitbox_radius / hitbox_height` のデータ分離。

つまり、CPU は正しい state、clip、frame を選択しており、最終的な shader sampling 座標だけが壊れていました。

## 3. 診断・検討経路

以下は、今回の問題を再現可能な形で切り分けた技術的な診断経路です。
CSV を推測で調整するのではなく、「state data」「sprite slicing」「GPU sampling interface」を順番に分離しました。

1. **画面上の矛盾から最小の問題を定義**
   画面には spitter の死亡 pose が表示されている一方、その敵は生存中で、プレイヤーによる kill もなく、
   runtime 上は移動状態でした。候補を state、clip selection、UV calculation、material upload、shader sampling に分解しました。

2. **data と state machine を先に検証**
   CSV の四つの origin、frame count、timing と、`EnemyState -> EnemyAnimationClipDefinition` の明示的 switch を確認しました。
   CPU snapshot の state／elapsed も renderer へそのまま渡されています。move state が move clip を選んでいたため、
   CSV label と AI state transition の不一致を除外しました。

3. **元 asset と透明領域を確認**
   二枚の PNG の dimensions、各 frame rectangle、alpha bounding box を確認しました。
   大きな透明余白は可視サイズの差を説明できますが、move row が dead row に置き換わることは説明できません。
   このため render size／hitbox を主原因から除外しました。

4. **CPU が算出する UV を検証**
   half-texel inset、frame X offset、state Y offset、V 軸方向、sheet bounds を確認しました。
   pure CPU helper は全 state で正しい値を返したため、問題は helper より後段にあると判断しました。

5. **material upload から shader ABI まで追跡**
   KamataEngine `Material::ConstBufferData`、MSVC の `offsetof`、`Obj.hlsli` の `packoffset` を比較しました。
   CPU の `uvOffset` は byte 60 から始まる一方、HLSL の `float3 m_uv_offset` は byte 64 から始まっていました。

6. **byte mapping から症状を予測**
   旧 shader は intended Y offset を U offset として読み、V offset をゼロとして読むことが分かりました。
   これは「水平 frame がずれる」「すべての state が別 row を参照する」という二つの症状を同時に予測し、実画面と一致しました。

7. **最小で検証可能な修正を選択**
   CSV や state machine を変更せず、プロジェクト HLSL だけで `uvOffset` を `c3.w + c4.xy` に分割しました。
   特殊な CPU swizzle も追加していません。診断が正しければ ABI 修正だけで全 state が復旧する設計です。

8. **反証可能な形で確認**
   shader、Debug／Release を再ビルドし、state-to-row test と実 GPU 表示を確認しました。
   生存中の blood dog が正しい move row を連続表示し、dead-row 症状が消えたため、constant-buffer layout が根因だと確認できました。

この経路で最も重要な判断は、**透明余白は可視サイズの差を説明できても row 全体の置き換わりは説明できず、
Y UV offset の消失または誤読だけが全症状を同時に説明できる**という点です。

## 4. 問題原因：CPU と GPU の実レイアウト

### 4.1 CPU 側

KamataEngine が公開している `Material::ConstBufferData` の末尾には、二つの連続した `Vector3` があります。

```cpp
Vector3 uvScale;
Vector3 uvOffset;
```

現在の Object_FPS の Windows x64／MSVC 構成では、実際の offset は次のとおりです。

| フィールド | Byte 範囲 | HLSL register 上の位置 |
| --- | ---: | --- |
| `uvScale.x/y/z` | 48–59 | `c3.xyz` |
| `uvOffset.x` | 60–63 | `c3.w` |
| `uvOffset.y` | 64–67 | `c4.x` |
| `uvOffset.z` | 68–71 | `c4.y` |

`EnemyBillboardRenderer.cpp` では、現在この ABI を `static_assert` で検証しています。

```cpp
offsetof(KamataEngine::Material::ConstBufferData, uvScale) == 48
offsetof(KamataEngine::Material::ConstBufferData, uvOffset) == 60
```

したがって、このレイアウトは推測ではなく、現在の build が実際に要求している条件です。

### 4.2 修正前の HLSL 側

元の `Obj.hlsli` は次の宣言でした。

```hlsl
float3 m_uv_scale  : packoffset(c3);
float3 m_uv_offset : packoffset(c4);
```

HLSL の一つの `float3` は 16-byte register 境界をまたぎません。そのため二つ目の `float3` は、
CPU の `uvOffset.x` が置かれている byte 60 ではなく、`c4.x`（byte 64）から始まります。

CPU が次の値を書き込んだ場合、

```text
uvOffset = (intendedOffsetX, intendedOffsetY, 0)
```

旧 shader が実際に読み取る内容は次のようになります。

```text
m_uv_offset.x = CPU uvOffset.y = intendedOffsetY
m_uv_offset.y = CPU uvOffset.z = 0
m_uv_offset.z = byte 72–75 の非契約データ
```

したがって pixel shader では、

```text
U offset <- row 用の Y offset を誤って使用
V offset <- 0
```

となります。V offset が失われるため、指定した state row へ移動できません。
さらに負の V scale と wrap sampler が組み合わさることで sheet 下部を参照しやすくなり、
生存中の敵が dead row を表示する直接的な原因になりました。

## 5. 解決策：今回の変更内容

`Obj.hlsli` では、CPU 側で register 境界をまたいでいる配置に合わせてフィールドを明示的に分割しました。

```hlsl
float3 m_uv_scale : packoffset(c3);
float  m_uv_offset_x : packoffset(c3.w);
float2 m_uv_offset_yz : packoffset(c4);
```

`ObjPS.hlsl` では、描画に必要な二次元 offset を再構築します。

```hlsl
const float2 uvOffset = float2(m_uv_offset_x, m_uv_offset_yz.x);
```

対応は次のようになります。

| Shader の値 | CPU 側の値 | 結果 |
| --- | --- | --- |
| `uvOffset.x` | `uvOffset.x`、byte 60 | 正しい水平方向の frame offset |
| `uvOffset.y` | `uvOffset.y`、byte 64 | 正しい垂直方向の state row offset |

この変更により、`KamataEngine::Material::uvOffset_` の公開 API が本来持つべき意味、つまり
CPU で設定した `(x, y, z)` を GPU でも同じ `(x, y, z)` として読む挙動が復元されました。

なお、`ObjPS.hlsl` の alpha cutoff は Atlas の透明領域が depth を書き込まないための別変更です。
state／row の不一致を起こした ABI 問題とは別の目的であり、分けて理解する必要があります。

## 6. プロジェクト側 HLSL を修正した理由

採用理由は次のとおりです。

1. Object_FPS はコンパイル済み KamataEngine library を利用しており、外部 header だけを変更しても library 内部の書き込みは変わらない。
2. Enemy Atlas は KamataEngine 公開 API の `Material::uvScale_ / uvOffset_` を正しく利用しており、CPU 側の呼び出し方は妥当である。
3. プロジェクト固有の `NoviceResources/shaders` が配布されるため、変更は Object_FPS 内に限定され、共有 runtime のインストール先を直接変更しない。
4. shader のフィールドを分割するだけで API の意味を復元でき、新しい root signature、descriptor、同期処理が不要である。
5. map、sky、通常の OBJ material は UV offset がゼロであり、修正前後の出力は同じである。可視差分は主に Atlas に限定される。

以上から、完成版 MVP に対しては、プロジェクト内 HLSL の ABI 修正が最も低コストかつ低リスクです。

## 7. 代替技術案

shader 修正は唯一の方法ではありません。以下の方式も実装可能です。

| 方式 | 共通 OBJ shader の変更 | 利点 | 欠点／リスク | 今回の判断 |
| --- | --- | --- | --- | --- |
| HLSL を CPU layout に合わせる（今回） | あり | 小規模、API の意味を復元、engine 再ビルド不要 | 将来 engine ABI が変わる場合は追従が必要 | MVP に最適 |
| CPU 側で `{0, offsetX, offsetY}` と意図的に swizzle | なし | 変更行数が少ない | 既知の不整合に依存、`uvOffset_` の意味を破壊、engine 修正後に再故障 | 非推奨 |
| Enemy 専用 shader／PSO／root signature／constant buffer | なし | 完全に分離できる | D3D12 pipeline、descriptor、寿命管理が大幅に増える | 可能だが MVP には過剰 |
| 独自 quad の vertex UV を frame ごとに更新 | なし | Material UV offset に依存しない | 動的 vertex buffer または多数の mesh、GPU 同期が必要 | 不採用 |
| Atlas を 33 枚の個別 texture に分割 | なし | offset を常にゼロにできる | asset、texture／descriptor、ロード負荷が増え、single-sheet 設計を失う | 不採用 |
| CPU struct に 4-byte padding を追加して KamataEngine を再ビルド | 旧 HLSL を維持可能 | engine レベルで `c4` に統一でき、長期的に最も明快 | library ABI が変わり、全 consumer の再ビルドが必要 | 最良の upstream 案だが MVP 範囲外 |

### 7.1 shader を変更しない最小 workaround

旧 HLSL の `m_uv_offset.x/y` が CPU の `uvOffset.y/z` を読むことを利用し、C++ 側を次のように書けば、
一時的には正しい二次元 offset を渡せます。

```cpp
material->uvOffset_ = {0.0f, intendedOffsetX, intendedOffsetY};
```

ただし、これは誤った layout を利用する workaround にすぎません。

- `uvOffset_.x` を意図的に捨てる。
- C++ のフィールド名と GPU 上の意味が一致しない。
- 正常な `(x, y, z)` を期待する他の material の問題は残る。
- KamataEngine が padding を修正した瞬間に X／Y が再び壊れる。

そのため短期診断には利用できますが、完成版 MVP の最終実装には適しません。

### 7.2 長期的に理想的な upstream 修正

将来 KamataEngine を保守し、library を再ビルドできる場合は、CPU constant-buffer struct に
明示的な padding を追加する案が推奨されます。

```cpp
Vector3 uvScale;
float uvScalePadding;
Vector3 uvOffset;
```

これにより `uvOffset` は byte 64／`c4.x` から始まり、元の HLSL
`float3 m_uv_offset : packoffset(c4)` を維持できます。16-byte 単位の constant-buffer 設計として自然ですが、
header、library、すべての consumer を同時に更新する必要があり、Object_FPS 側だけでは完結しません。

## 8. 影響範囲と互換性

### 8.1 影響を受ける範囲

- `Obj.hlsli / ObjPS.hlsl` を利用し、非ゼロの `Material::uvOffset_` を設定する KamataEngine Model。
- Enemy Atlas の水平 frame 選択と垂直 state row 選択。

### 8.2 Gameplay への影響なし

- CSV の state、frame count、event frame、muzzle データ。
- Enemy AI、damage、hitbox、navigation、attack timing、death timing。
- `render_width / render_height` と hitbox の分離規則。

### 8.3 他の pipeline への影響なし

- Sprite／HUD／UI shader。
- Scene post-process shader。
- Projectile gameplay。
- UV offset がゼロの map／sky material。これらは修正前後とも入力がゼロです。

## 9. 検証と回帰防止

今回の修正には、目視確認だけでなく次の防止策があります。

1. **コンパイル時 ABI 検査**
   `EnemyBillboardRenderer.cpp` の `static_assert` で `uvScale == 48`、`uvOffset == 60` を検証します。

2. **Shader source contract test**
   `c3.w / c4` に分割した宣言が存在し、旧 `float3 m_uv_offset : packoffset(c4)` が存在しないことを確認します。

3. **State-to-row test**
   `idle / move / attack / dead` から対応する Atlas origin／frame への mapping を検証します。

4. **Shader compile**
   `ObjVS.hlsl` と `ObjPS.hlsl` は Shader Model 5 のコンパイルに成功しています。

5. **Fresh build／CTest**
   Debug／Release の旧生成物を削除して完全ビルドし、両構成の CTest が成功しています。

6. **実 GPU 表示**
   修正前は生存中の敵が spitter の dead frame を表示しましたが、修正後は blood dog の移動中に正しい move row が連続表示されました。

## 10. 潜在リスク

| リスク | 想定される結果 | 現在の対策 | 残存度 |
| --- | --- | --- | --- |
| 将来 KamataEngine が `uvOffset` を byte 64 に整列する | 現 HLSL が新 ABI と不一致になる | `static_assert` が非互換 build をコンパイル時に停止する | 低。ただし engine 更新時に対応必須 |
| project shader が KamataEngine 原本から分岐する | runtime shader 更新時に修正が上書き・未 merge になる | version control、本文書、source contract test、配布 hash 照合 | 中低 |
| `Obj.hlsli` が共通 Model interface である | 非 enemy object にも layout 変更が及ぶ可能性 | offset がゼロの map／sky は新旧で同じ結果。Debug／Release smoke test 済み | 低 |
| 現 Windows x64／MSVC packing への依存 | compiler、platform、Vector3 定義変更で offset が変わる | 文書上の仮定ではなく `offsetof` を build 時に検証 | 低 |
| source-string test は完全な shader reflection ではない | 宣言文字列が正しくても binding 全体の問題を見逃す可能性 | FXC compile、CPU mapping test、実 GPU 表示を併用 | 低 |
| HLSL だけを元に戻し Atlas upload を残す | state／frame の誤表示が再発する | UV 伝達経路全体を置換しない限り修正を単独で削除しない | 中 |
| 外部 library 実装だけが変わり header offset が同じ | `static_assert` が内部 semantic change を検知できない可能性 | engine 更新後も runtime Atlas smoke test を継続 | 低 |

性能面では、同じ constant-buffer data を正しい位置から読むだけであり、texture sample、draw call、descriptor、
GPU resource は増えていません。追加した `float2` の再構築は通常 shader compiler により最適化されるため、
実質的な性能リスクはありません。

alpha cutoff には、エッジの aliasing、遠距離 mip bleed、`discard` cost など一般的な透明 material のリスクがあります。
ただしこれは `ObjPS.hlsl` の透明処理に関する事項であり、今回の `Obj.hlsli` ABI 修正そのもののリスクではありません。

## 11. 将来の保守上の注意

- KamataEngine 更新時は、最初に `offsetof(Material::ConstBufferData, uvOffset)` を再確認する。
- offset が 60 から 64 に変わった場合、engine 側に padding が追加されたことを意味する。その場合は
  HLSL を `c4` の完全な `float3` に戻し、`static_assert` と test も同時に更新する。
- 現在の `static_assert` により、互換性のない engine ABI は誤表示ではなくコンパイルエラーとして検出される。
- HLSL の修正だけを削除してはならない。別方式へ移行する場合は、UV offset の伝達経路を同時に置き換える必要がある。
- 長期的には C++／HLSL の共通 layout 定義、shader reflection、または自動 ABI test の導入が望ましい。

## 12. 最終判定

- **変更理由**：CPU material constant buffer と GPU HLSL の byte layout を一致させ、Atlas X／Y offset を正しく読み取るため。
- **以前から存在したバグか**：はい。ただしゼロ offset によって隠れていた潜在的不具合で、Enemy Atlas の非ゼロ offset により初めて顕在化した。
- **他の方式はあるか**：あります。CPU swizzle、敵専用 pipeline、動的 UV、texture 分割、KamataEngine 再ビルドなどが可能です。
- **今回 HLSL を変更した理由**：外部 engine を再ビルドせず single Atlas を維持する条件で、最小かつ明快で、既存 Material API の意味にも合う修正だったためです。
