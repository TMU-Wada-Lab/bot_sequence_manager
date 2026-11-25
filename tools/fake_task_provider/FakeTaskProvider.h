// -*- C++ -*-
/*!
 * @file  FakeTaskProvider.h
 * @brief テスト用の偽 Task_provider の中身（OpenRTM に依存しない）
 *
 * 受けたコマンドを「実行中 → 一定時間後に完了」として、Bot ごとのシステム状態を返すだけ。
 * 実機・Bot_controller なしで CSV管理や GUI の動作を確かめるために使う。
 *
 *  ・PAUSE           ：実行中のコマンドを中断（結果＝中断）して待機へ
 *  ・ESTOP_ENABLE    ：非常停止へ（実行中のコマンドは中断）
 *  ・ESTOP_DISENABLE ：未準備へ（原点出しのやり直しが必要、MECHA_HOME の完了で待機へ）
 *  ・CLEAR_ERROR     ：エラーから待機へ
 *  ・失敗のまねごと  ：fail_command に指定したコマンドを 1 回だけ失敗または拒否させる
 */
#ifndef FAKE_TASK_PROVIDER_H
#define FAKE_TASK_PROVIDER_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fake_tp {

using Packet = std::vector<int32_t>;

struct Settings {
    std::vector<int> bot_ids{1};   // 登録済みの Bot
    int  initial_state   = 1;      // 起動時の主状態（BOT_STATE：0 未準備／1 待機）
    int  step_time_ms    = 2000;   // コマンドが完了するまでの時間
    int  fail_command    = -1;     // 失敗させるコマンド番号（-1 で失敗させない）
    int  fail_mode       = 1;      // 1：失敗（FAILED）／2：拒否（REJECTED）
    int  fail_code       = 1;      // 失敗：ERROR_CODE／拒否：REJECT_REASON
    int  fail_resumable  = 1;      // 失敗のときの再開可否（0／1）
};

class FakeTaskProvider {
public:
    explicit FakeTaskProvider(const Settings& s);

    void on_command(const Packet& cmd, int64_t now_ms);
    void tick(int64_t now_ms);                 // 完了の判定
    std::vector<Packet> states() const;        // 全 Bot のシステム状態
    std::vector<std::string> take_logs();

private:
    struct Bot {
        int     state      = 1;
        int     command    = -1;
        int     result     = 1;   // RESULT
        int     error_code = 0;
        int     resumable  = 0;
        bool    busy       = false;
        int64_t done_at    = 0;
    };

    void command_for_bot(int bot_id, Bot& b, int cmd, int64_t now_ms);
    void finish(Bot& b, int result, int error_code = 0);
    void log(const std::string& s) { logs_.push_back(s); }

    Settings settings_;
    bool failed_once_ = false;
    std::map<int, Bot> bots_;
    std::vector<std::string> logs_;
};

}  // namespace fake_tp

#endif  // FAKE_TASK_PROVIDER_H
