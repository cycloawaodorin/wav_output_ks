# WAVファイル出力 プラグイン
AviUtl ExEdit2 用のWAVファイル出力プラグインです．

## インストール
[Releases](https://github/cycloawaodorin/wav_output_ks/releases) から `wav_output_ks.VERSION.zip` をダウンロード，展開します．できた`wav_output_ks.auo2`を，AviUtl ExEdit2 のプレビュー画面にドラッグ&ドロップし，インストールを許可します．

## アンインストール
`%ProgramData%\Plugin\wav_output_ks.auo2` と `%ProgramData%\Plugin\wav_output_ks.config` を削除します．`data` フォルダを設定している場合は，それ以下の同等のファイルを削除します．

## 設定
<dl>
 <dt>16bit short / 32bit float</dt>
  <dd>出力フォーマットを <code>WAVE_FORMAT_PCM</code> か <code>WAVE_FORMAT_IEEE_FLOAT</code> から選択します．</dd>
 <dt>ステレオ</dt>
  <dd>ステレオ(2チャンネル)で出力します．プロジェクトがモノラルなら同じデータを複製し，3チャンネル以上なら先頭2チャンネルのみ出力します．</dd>
 <dt>モノラル</dt>
  <dd>モノラル(1チャンネル)で出力します．プロジェクトが複数チャンネルを持つなら，全チャンネルの平均を出力します．</dd>
 <dt>オート</dt>
  <dd>音声チャンネル数をプロジェクトのチャンネル数と同じで出力します．</dd>
</dl>

## Contributing
バグ報告等は https://github.com/cycloawaodorin/wav_output_ks/issues にお願いします．
