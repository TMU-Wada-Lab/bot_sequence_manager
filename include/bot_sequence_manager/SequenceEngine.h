// -*- C++ -*-
/*!
 * @file  SequenceEngine.h
 * @brief CSV管理の中身：Bot ごとのシーケンス状態を持ち、ステップを 1 つずつ送る
 *
 * OpenRTM に依存しない（RTC からは配列を渡し、配列を受け取るだけ）。
 *
 *  入力
 *   - on_seq_command()  ：GUI からのシーケンス指令（CMD_PACKET 形式）
 *   - on_system_state() ：Task_provider のシステム状態（SYS_STATE_PACKET 形式、GUI 経由）
 *  出力
 *   - take_commands()       ：Task_provider へ送るコマンド（CMD_PACKET 形式、GUI 経由）
 *   - take_changed_states() ：状態が変わった Bot のシーケンス状態（SEQ_STATE_PACKET 形式）
 *   - all_states()          ：全 Bot のシーケンス状態（周期送信用）
 *   - take_logs()           ：ログに出す文
 */
#ifndef BOT_SEQUENCE_MANAGER_SEQUENCE_ENGINE_H
#define BOT_SEQUENCE_MANAGER_SEQUENCE_ENGINE_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "SequenceCsv.h"

namespace seqmgr {

using Packet = std::vector<int32_t>;

class SequenceEngine {
public:
    explicit SequenceEngine(SequenceTable table = SequenceTable());

    void on_seq_command(const Packet& pkt);
    void on_system_state(const Packet& pkt);

    std::vector<Packet> take_commands();
    std::vector<Packet> take_changed_states();
    std::vector<Packet> all_states() const;
    std::vector<std::string> take_logs();

    // テスト・デバッグ用
    milo::SEQUENCE_STATE sequence_state(int bot_id) const;
    int current_step(int bot_id) const;  // 1 始まり、なしは 0

private:
    // Task_provider から見えている Bot の最新状態
    struct BotStatus {
        bool known           = false;
        int  bot_state       = -1;
        int  running_command = -1;
        int  result          = -1;
        int  error_code      = 0;
        int  resumable       = 0;
    };

    struct BotSequence {
        milo::SEQUENCE_STATE state = milo::SEQUENCE_STATE::NONE;
        int    sequence_id = 0;
        size_t index       = 0;      // 実行中・中断中のステップ（0 始まり）
        bool   awaiting    = false;  // ステップを送って、結果を待っている
        // 送った時点のシステム状態が「同じコマンドの終わった結果」だった場合、
        // それは前回の結果なので、状態が一度変わるまで無視する
        bool   ignore_stale = false;
        int    stale_result = -1;
        int    stale_error  = 0;

        int  stop_code      = 0;     // 中断の原因（ERROR_CODE／REJECT_REASON）
        bool step_resumable = true;  // 中断したステップを RESUME できるか

        int last_request  = -1;      // 直近のシーケンス指令
        int last_result   = 0;
        int reject_reason = 0;

        BotStatus status;
    };

    void handle_start(int bot_id, int target);
    void handle_pause(int bot_id);
    void handle_resume(int bot_id);
    void handle_cancel(int bot_id);
    void handle_estop(int bot_id);

    void accept(int bot_id, milo::COMMAND request);
    void reject(int bot_id, milo::COMMAND request, milo::REJECT_REASON reason);
    void suspend(int bot_id, int stop_code, bool resumable);
    void send_current_step(int bot_id);
    void advance(int bot_id);

    bool can_resume(const BotSequence& b) const;
    Packet make_state(int bot_id, const BotSequence& b) const;
    void log(const std::string& s) { logs_.push_back(s); }

    SequenceTable table_;
    std::map<int, BotSequence> bots_;
    std::set<int> dirty_;
    std::vector<Packet> commands_;
    std::vector<std::string> logs_;
};

const char* command_name(int command);

}  // namespace seqmgr

#endif  // BOT_SEQUENCE_MANAGER_SEQUENCE_ENGINE_H
