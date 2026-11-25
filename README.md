# bot_sequence_manager（CSV管理）

2台目 MiLo Bot システムの **CSV管理** コンポーネントです。シーケンス CSV（`bot_sequence.csv`）を起動時に読み込みます。GUI から START_SEQUENCE を受けると、その Bot へ手順を 1 ステップずつ送り、完了を確かめてから次のステップを送ります。

- OpenRTM-aist 2.0.1（C++）/ Visual Studio 2022 x64 / CMake 3.16 以上
- 番号の定義は [bot_system_def](https://github.com/TMU-Wada-Lab/bot_system_def) の `bot_command_def.h`・`bot_sequence_csv.h` を使います

## 構成

```text
             seq_cmd（シーケンス指令）
 コマンド入力 ─────────────────▶ ┌──────────────────────┐
   （GUI）   ─────────────────▶ │ bot_sequence_manager │ ◀── bot_sequence.csv（起動時に読み込み）
             sys_state（システム状態）│     （CSV管理）      │
             ◀───────────────── │                      │
             cmd（コマンド）      │                      │
             ◀───────────────── └──────────────────────┘
             seq_state（シーケンス状態）
```

| ポート | 向き | 型 | 中身（bot_command_def.h） |
|---|---|---|---|
| `seq_cmd` | In | TimedLongSeq | シーケンス指令（`CMD_PACKET`）。START_SEQUENCE・PAUSE・RESUME・CANCEL・ESTOP_ENABLE |
| `sys_state` | In | TimedLongSeq | Task_provider のシステム状態（`SYS_STATE_PACKET`）。GUI が転送する |
| `cmd` | Out | TimedLongSeq | ステップのコマンド（`CMD_PACKET`）。GUI が Task_provider へ転送する |
| `seq_state` | Out | TimedLongSeq | シーケンス状態（`SEQ_STATE_PACKET`）。Bot ごとに 1 配列 |

## 動作

### シーケンス状態の移り変わり（Bot ごとに独立）

| 今の状態 | きっかけ | 次の状態 | すること |
|---|---|---|---|
| なし | START_SEQUENCE | 実行中 | 1 ステップ目を送る |
| 実行中 | ステップの結果が完了 | 実行中 | 次のステップを送る（最後なら「なし」へ） |
| 実行中 | PAUSE・ESTOP_ENABLE | 中断中 | 次を送らない |
| 実行中 | ステップの結果が失敗・拒否・中断 | 中断中 | 原因（エラーコード・拒否理由）を記録 |
| 中断中 | RESUME | 実行中 | 止まったステップを送り直す |
| 中断中 | CANCEL | なし | 手順を捨てる |

- ステップの完了は、システム状態の「実行中のコマンド」がそのステップのコマンドで、「結果」が 1（完了）になったことで判断します。
- 送った時点で同じコマンドの古い結果（前回の完了など）が見えていた場合は、状態が一度変わるまでその結果を無視します（取り違え防止）。
- PAUSE と同時にステップが完了した場合は、次のステップで止めておき、RESUME でそのステップから送ります。

### 受け付ける条件（満たさないときは拒否し、シーケンス状態で理由を返す）

| 指令 | 条件 | 拒否理由 |
|---|---|---|
| START_SEQUENCE | 宛先が 1〜99、CSV にあるシーケンス | INVALID_PARAM |
| | シーケンスが「なし」 | 実行中：BUSY／中断中：INVALID_STATE（先に CANCEL） |
| | その Bot のシステム状態を受信済み、コマンド実行中でない | NOT_READY／BUSY |
| | Bot が待機（PREPARE だけは未準備でも可） | NOT_READY／INVALID_STATE |
| PAUSE | 実行中（宛先 0 は実行中の全 Bot） | INVALID_STATE |
| RESUME | 中断中、止まったステップの結果を受信済み | INVALID_STATE／BUSY |
| | 再開できるエラーで止まった | INVALID_STATE（再開不可のエラーは CANCEL して PREPARE から） |
| | Bot が待機 | エラー中：NOT_RESOLVED（先に CLEAR_ERROR）／未準備：NOT_READY |
| CANCEL | 中断中 | INVALID_STATE |
| ESTOP_ENABLE | いつでも受け付ける（宛先 0 は全 Bot） | － |

## ビルド

`bot_system_def` を同じフォルダ（`workspace/`）に置いた場合の例です（PowerShell）。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DOpenRTM_DIR="C:/Program Files/OpenRTM-aist/2.0.1/cmake"
cmake --build build --config Release
```

別の場所にある場合は `-DBOT_SYSTEM_DEF_DIR=<bot_system_def のパス>` を付けます。生成物は `build/Release/` に出ます。

| ファイル | 内容 |
|---|---|
| `bot_sequence_managerComp.exe` | RTC 本体（単体起動用） |
| `bot_sequence_manager.dll` | RTC 本体（マネージャーから読み込む用） |
| `sequence_engine_test.exe` | 単体テスト |

## 単体テスト

CSV の読み込みとシーケンスの状態遷移を、OpenRTM なしで確かめます。

```powershell
.\build\Release\sequence_engine_test.exe     # 「不合格 0」なら OK
```

OpenRTM のない PC では `-DBUILD_RTC=OFF` を付けるとテストだけビルドできます。

## 起動

このフォルダ（`rtc.conf` がある場所）から起動します。CSV のパスは起動したフォルダからの相対パスです。

```powershell
.\build\Release\bot_sequence_managerComp.exe -f rtc.conf
```

アクティブ化したときに CSV を読み込みます。CSV に書き間違いがあると、行番号つきのメッセージを出してエラー状態になります。CSV を直したら、RTC をリセットしてアクティブ化し直してください。非アクティブ化すると、実行中・中断中の手順は捨てられます。

## 設定（`bot_sequence_manager.conf`）

| パラメーター | 既定値 | 内容 |
|---|---|---|
| `csv_file` | `../bot_system_def/bot_sequence.csv` | 読み込むシーケンス CSV |
| `state_period_ms` | `500` | シーケンス状態を全 Bot 分まとめて送る周期 [ms]（0 で変化時のみ） |
| `exec_cxt.periodic.rate` | `50.0` | 実行周期 [Hz] |

## 注意

- 1 回の実行周期で複数の配列（複数 Bot 分など）を続けて書き込みます。ポートを接続するときは、データの送り方（サブスクリプション型）を既定の `flush` のままにしてください（`new` にすると途中の配列が抜けます）。
- 同じコマンドが、まったく同じ結果とエラーコードで続けて返ってくると、新しい結果か古い結果かを見分けられず、待ち続けます（例：同じステップが同じ理由で 2 回続けて拒否された）。そのときは PAUSE → CANCEL で抜けてください。コマンドに通し番号（cmd_id）を付けない設計上の制約です。
