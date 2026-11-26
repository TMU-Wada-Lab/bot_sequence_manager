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

結合テスト（CSV管理のロジック ⇄ 偽の Task_provider を、時間を進めながらつないで動かす）もあります。

```powershell
.\build\Release\integration_test.exe         # PREPARE→NORMAL、PAUSE/RESUME、失敗からの復旧、非常停止（2 台）
```

## 偽の Task_provider でつないで試す

本物の Task_provider・GUI がなくても、RTC 同士をつないで CSV管理を試せます。

| 道具 | 中身 |
|---|---|
| `fake_task_providerComp.exe` | 偽の Task_provider。受けたコマンドを「実行中 → `step_time_ms` 後に完了」としてシステム状態を返す。PAUSE・ESTOP・CLEAR_ERROR・原点出しの流れもまねる |
| `tools/gui_stub.py` | GUI の代わり。START_SEQUENCE → PAUSE → RESUME を送り、シーケンス状態の変化を表示する |

```powershell
# 1. ネームサーバー（rtm-naming.bat など）を起動しておく
# 2. この 2 つを起動
.\build\Release\bot_sequence_managerComp.exe -f rtc.conf
.\build\Release\fake_task_providerComp.exe -f rtc.conf
# 3. つないでアクティブにする（rtshell）
$H = "/localhost:2809/$(hostname).host_cxt"
rtcon $H/bot_sequence_manager0.rtc:cmd $H/fake_task_provider0.rtc:cmd
rtcon $H/fake_task_provider0.rtc:sys_state $H/bot_sequence_manager0.rtc:sys_state
rtact $H/bot_sequence_manager0.rtc $H/fake_task_provider0.rtc
# 4. GUI の代わりを動かす（ポートの接続は自動）
python tools\gui_stub.py
```

偽の Task_provider の設定（RTSystemEditor の Configuration で変えられる）：

| パラメーター | 既定値 | 内容 |
|---|---|---|
| `bot_ids` | `1` | 登録済みの Bot（`1,2` のようにカンマ区切り） |
| `initial_state` | `1` | 起動時の主状態（0 未準備／1 待機） |
| `step_time_ms` | `2000` | コマンドが完了するまでの時間 |
| `fail_command` | `-1` | 1 回だけ失敗させるコマンド番号（例：9＝EXCHANGE、-1 で失敗させない） |
| `fail_mode` | `1` | 1 失敗（FAILED）／2 拒否（REJECTED） |
| `fail_code` | `1` | 失敗なら ERROR_CODE、拒否なら REJECT_REASON |
| `fail_resumable` | `1` | 失敗したときの再開可否 |

※ rtshell の `rtinject`・`rtprint` は、Python 3.11 以降では動きません（`inspect.getargspec` が削除されたため）。データの送受信を試すときは `gui_stub.py` を使ってください。`rtcon`・`rtact`・`rtls` は動きます。

## 起動

exe をダブルクリックしても、コマンドで起動してもかまいません（`fake_task_providerComp.exe` も同じ）。

```powershell
# コマンドで起動するとき（このフォルダで）
.\build\Release\bot_sequence_managerComp.exe -f rtc.conf
```

`rtc.conf`・`bot_sequence_manager.conf`・CSV のパスは、起動したフォルダから見た相対パスで書いてあります。そのため `-f` を付けずに起動したときは、exe が自分で起動フォルダを決めます。

| 起動のしかた | 起動フォルダ |
|---|---|
| `-f` を付ける | そのまま（指定した設定ファイルを使う） |
| `-f` なし、起動したフォルダに `rtc.conf` がある | そのまま（`-f rtc.conf` を補う） |
| `-f` なし、`rtc.conf` がない（ダブルクリックなど） | exe のあるフォルダから上へ `rtc.conf` を探し、見つかったフォルダ（このフォルダ）へ移る |

コンソールに `起動フォルダ: …` と出たら、フォルダを移っています。Debug・Release のどちらの exe でも、このフォルダが起動フォルダになります。

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
