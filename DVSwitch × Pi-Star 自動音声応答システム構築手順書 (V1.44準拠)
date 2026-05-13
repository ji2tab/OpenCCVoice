# DVSwitch × Pi-Star 自動音声応答システム構築手順書 (V1.44準拠)

DVSwitchとPi-Starを連携させ、Open JTalkによる高品質な音声合成を用いた「自動音声応答システム」の構築手順をまとめたドキュメントです。

このシステムは、無線機からのカーチャンク（短時間の送信）を検知し、適切な「間（ま）」を置いた後に、相手のコールサインを読み上げて応答する **「CCVoice」** のロジックを継承しています。

---

## 概要

| 項目 | 内容 |
|------|------|
| 対象環境 | Pi-Star / WPSD |
| 音声合成エンジン | Open JTalk |
| 音声モデル | MMDAgent「メイ」(mei_normal.htsvoice) |
| 連携方式 | DVSwitch Analog_Bridge (USRP) への UDP 音声注入 |
| 音声フォーマット | 8000Hz / 16bit / モノラル |
| メインスクリプト | `dvswitch_bot.py` (V1.44) |

---

## 1. システムの準備と書き込み可能モードへの移行

Pi-Star環境はデフォルトで書き込み制限がかかっているため、設定変更の前に書き込み可能モードに移行する必要があります。

まずはファイルシステムを書き込み可能にし、必要なパッケージ（Open JTalk, SoX, unzip）をインストールします。

```bash
# 書き込み可能モードへ変更
rpi-rw

# パッケージリストの更新と必要なソフトのインストール
sudo apt-get update
sudo apt-get install -y open-jtalk open-jtalk-mecab-naist-jdic sox unzip
```

### 確認事項

- ターミナルのプロンプトが `(rw)` になっていること
- `md380-emu`（ソフトウェアAMBE）および `Analog_Bridge` が稼働していること

---

## 2. 高品質音声モデル（メイ）の導入

標準の音声よりも流暢な「メイ（MMDAgent）」モデルを導入し、読み上げの質を向上させます。
MMDAgentの公式サイトからデータをダウンロードし、所定のディレクトリに配置します。

```bash
# 一時ディレクトリに移動
cd /tmp

# MMDAgentのサンプルデータをダウンロード
wget https://sourceforge.net/projects/mmdagent/files/MMDAgent_Example/MMDAgent_Example-1.8/MMDAgent_Example-1.8.zip

# 解凍して音声モデルを取り出す
unzip MMDAgent_Example-1.8.zip
sudo mkdir -p /usr/share/hts-voice/mei
sudo cp MMDAgent_Example-1.8/Voice/mei/mei_normal.htsvoice /usr/share/hts-voice/mei/

# ゴミ掃除（不要になったZIPファイルや展開ディレクトリを削除）
rm -rf MMDAgent_Example-1.8.zip MMDAgent_Example-1.8
```

---

## 3. DVSwitch (Analog_Bridge) の設定変更と再起動

PythonスクリプトからのUDP音声パケットを受け入れるため、設定ファイルを編集します。

```bash
# 設定ファイルをエディタ(nano)で開く
sudo nano /opt/Analog_Bridge/Analog_Bridge.ini
```

### nanoエディタでの編集内容

`[USRP]` セクションを探し、以下のように書き換えます（※自己ループと致命的エラーを回避します）。

```ini
[USRP]
rxPort = 51000
txPort = 51001
;jitterQueueSize = 30  ← 先頭に「;」をつけてコメントアウト
;pcmBufferMS = 200     ← 先頭に「;」をつけてコメントアウト
```

> **保存方法:** `Ctrl+O` → `Enter` で保存し、`Ctrl+X` で閉じます

### 設定のポイント

- **ポート設定:** `rxPort` を `51000` に、`txPort` を `51001` に設定し、自己ループを防止
- **互換性調整:** バージョンによっては `jitterQueueSize` や `pcmBufferMS` が存在するとエラーで停止するため、行頭に `;` を付けてコメントアウト

### サービスの再起動

```bash
# 変更を反映してAnalog_Bridgeを再起動
sudo systemctl restart Analog_Bridge
```

設定反映後、Analog_Bridge が正常に起動することを確認してください。

---

## 4. システムディレクトリの作成と固定WAVの生成

応答速度を上げるため、メッセージの固定部分はあらかじめWAVファイルとして作成しておく **「ハイブリッド方式」** を採用します。

毎回計算すると遅延になる「前後の定型文」を、予めDVSwitch仕様（8000Hz/16bit/モノラル）のWAVとして作成しておきます。
ここでは以下のパラメータを適用しています:

| パラメータ | 値 | 効果 |
|-----------|----|------|
| `-r 0.85` | スピード | ゆっくりめの読み上げ |
| `-fm 1.3` | ピッチ | 少し高めの声 |
| `highpass 300` | フィルタ | 低域ノイズの除去 |
| `vol 1.5` | 音量 | 音量最適化 |

### ディレクトリ作成

```bash
# ボット用のディレクトリを作成
sudo mkdir -p /opt/dvswitch_bot
sudo chown pi-star:pi-star /opt/dvswitch_bot
cd /opt/dvswitch_bot
```

### 固定WAVの生成

#### 1. 前半部分（fixed_start.wav）の生成

「こちらはJJ2YYK尾張旭DMRデジピーターです」

```bash
echo "こちらは、ジェイジェイツーワイワイケー、おわりあさひディーエムアールデジピーターです" | open_jtalk -x /var/lib/mecab/dic/open-jtalk/naist-jdic -m /usr/share/hts-voice/mei/mei_normal.htsvoice -r 0.85 -fm 1.3 -ow /tmp/start_48k.wav
sudo sox /tmp/start_48k.wav -r 8000 -c 1 -b 16 /opt/dvswitch_bot/fixed_start.wav highpass 300 vol 1.5
```

#### 2. 後半部分（fixed_end.wav）の生成

「局からのカーチャンクです」

```bash
echo "局からのカーチャンクです" | open_jtalk -x /var/lib/mecab/dic/open-jtalk/naist-jdic -m /usr/share/hts-voice/mei/mei_normal.htsvoice -r 0.85 -fm 1.3 -ow /tmp/end_48k.wav
sudo sox /tmp/end_48k.wav -r 8000 -c 1 -b 16 /opt/dvswitch_bot/fixed_end.wav highpass 300 vol 1.5
```

> **ヒント:** 読み上げの「間」を開けたい場合は、`echo` の中の文章に「読点（、）」を追加して再度上のコマンドを実行すれば上書きされます。

---

## 5. メインプログラムの配置と読み取り専用モードへの復帰

最後にPythonスクリプト（`dvswitch_bot.py`）を `/opt/dvswitch_bot/` の中に配置し、システムを保護状態に戻します。

```bash
# (ここに dvswitch_bot.py を nano 等で作成/配置します)
sudo nano /opt/dvswitch_bot/dvswitch_bot.py

# 実行権限を付与
sudo chmod +x /opt/dvswitch_bot/dvswitch_bot.py

# システムを読み取り専用（安全な状態）に戻す
rpi-ro
```

### dvswitch_bot.py の主な機能 (V1.44)

| 機能 | 内容 |
|------|------|
| **PRE/POST 無音パケット** | 送信の前後 2.0 秒間に無音データを送出し、無線機の頭切れを完全に防止 |
| **受信時間フィルタ** | 0.5 秒〜1.5 秒の受信のみに反応し、ノイズや長時間の会話への誤反応を防止 |
| **20秒の抑止期間** | 送信終了後 20 秒間は、同じ局に対して二重に応答しないよう制限 |
| **ハイブリッド音声合成** | 固定WAV + 動的TTS（コールサイン読み上げ）を組み合わせて応答速度を向上 |

---

## 手動での起動テスト

動作テストを行う場合は、以下のコマンドで直接起動し、ログを画面で見ながら無線機でカーチャンクしてみてください。

```bash
python3 /opt/dvswitch_bot/dvswitch_bot.py
```

---

## 動作の流れ

```
[無線機] カーチャンク送信 (0.5〜1.5秒)
    ↓
[Pi-Star] 受信検知
    ↓
[dvswitch_bot.py] 受信時間フィルタ判定
    ↓
[応答シーケンス開始]
    ├─ PRE 無音パケット (2.0秒) ── 頭切れ防止
    ├─ fixed_start.wav 再生 ──── 「こちらはJJ2YYK尾張旭…」
    ├─ 動的TTS再生 ───────── コールサイン読み上げ
    ├─ fixed_end.wav 再生 ───── 「局からのカーチャンクです」
    └─ POST 無音パケット (2.0秒) ─ 尻切れ防止
    ↓
[20秒抑止期間] 同一局への二重応答を防止
```

---

## トラブルシューティング

| 症状 | 確認ポイント |
|------|------------|
| 音声が再生されない | `Analog_Bridge.ini` のポート設定 (`rxPort=51000`, `txPort=51001`) |
| Analog_Bridge が起動しない | `jitterQueueSize` / `pcmBufferMS` がコメントアウトされているか |
| 頭切れ・尻切れが発生 | PRE/POST 無音パケットの秒数調整 |
| 読み上げが不自然 | `echo` 内のカタカナ表記・読点（、）の追加 |
| 設定ファイルが保存できない | `rpi-rw` で書き込み可能モードに切替済みか |

---

## 作業完了後の重要事項

> ⚠️ **必ず読み取り専用モードに戻すこと**
>
> 作業完了後は、`rpi-ro` コマンドでシステムを読み取り専用モードに戻してください。
> 書き込み可能モードのままだとSDカードの寿命を縮め、突然の電源断によるファイルシステム破損のリスクが高まります。

```bash
rpi-ro
```

---

*Document Version: V1.44 準拠 / Pi-Star・WPSD 環境対応*
