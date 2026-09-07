# Optional runtime audio

このフォルダーの WAV は任意です。ファイルがない場合、その音だけを無効にして
ゲームは通常どおり起動します。ビルド時は `NoviceResources/` 全体と一緒に
`Resources/audio/` へコピーされます。

- `bgm.wav`：ゲーム中のループ BGM。
  - TODO(audio-assets): 最終版の BGM WAV を追加する。
  - TODO(audio-mix): 最終素材に合わせて BGM 音量を調整する。
- `level_select.wav`：レベルを選択したときの効果音。
  - TODO(audio-assets): 最終版のレベル選択 WAV を追加する。
  - TODO(audio-mix): 最終素材に合わせてレベル選択音量を調整する。
- `node_select.wav`：接続元ノードを選択したときの効果音。
  - TODO(audio-assets): 最終版のノード選択 WAV を追加する。
  - TODO(audio-mix): 最終素材に合わせてノード選択音量を調整する。

現在のコード上の仮音量はすべて `1.0` です。WAV を追加するときは PCM WAV として
書き出し、実機でクリッピングと BGM／効果音のバランスを確認してください。
