// -*- C++ -*-
/*!
 * @file  SequenceEngine.cpp
 * @brief CSV管理の中身（シーケンスの状態遷移）
 *
 * シーケンス状態の遷移
 *   なし ──START_SEQUENCE──▶ 実行中 ──最後のステップが完了──▶ なし
 *   実行中 ──PAUSE・ESTOP_ENABLE・ステップの失敗／拒否／中断──▶ 中断中
 *   中断中 ──RESUME（止まったステップを再送）──▶ 実行中
 *   中断中 ──CANCEL──▶ なし
 */
#include "SequenceEngine.h"

namespace seqmgr {

using milo::COMMAND;
using milo::REJECT_REASON;
using milo::RESULT;
using milo::SEQUENCE_STATE;
using milo::to_int;

namespace {

int at(const Packet& p, milo::CMD_PACKET i) {
    const auto k = static_cast<size_t>(to_int(i));
    return k < p.size() ? p[k] : 0;
}

int at(const Packet& p, milo::SYS_STATE_PACKET i) { return p[static_cast<size_t>(to_int(i))]; }

bool is_bot_id(int id) { return id >= milo::BOT_ID_MIN && id <= milo::BOT_ID_MAX; }

bool is_finished(int result) {
    return result == to_int(RESULT::COMPLETED) || result == to_int(RESULT::FAILED) ||
           result == to_int(RESULT::REJECTED) || result == to_int(RESULT::ABORTED);
}

}  // namespace

const char* command_name(int command) {
    static const char* const names[] = {
        "SELF_DIAG", "LOAD_MAP", "MECHA_HOME", "START_SEQUENCE", "PAUSE", "RESUME",
        "CANCEL", "RECEIVE", "MOVE", "EXCHANGE", "SEND_OUT", "RETREAT", "CLEAR_ERROR",
        "RESET_POSE", "ESTOP_ENABLE", "ESTOP_DISENABLE", "SET_MODE", "UNIT_OPERATE",
        "SET_PARAM", "MAP_START", "MAP_END", "SHUTDOWN", "RELEASE_STATION"};
    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(to_int(COMMAND::NUM)),
                  "command_name の表を COMMAND に合わせてください");
    if (command < 0 || command >= to_int(COMMAND::NUM)) return "(unknown)";
    return names[command];
}

SequenceEngine::SequenceEngine(SequenceTable table) : table_(std::move(table)) {}

// -----------------------------------------------------------------------------
// GUI からのシーケンス指令
// -----------------------------------------------------------------------------
void SequenceEngine::on_seq_command(const Packet& pkt) {
    if (pkt.size() < 2) {
        log("シーケンス指令の配列が短すぎるので無視しました");
        return;
    }
    const int dest    = at(pkt, milo::CMD_PACKET::DEST_ID);
    const int command = at(pkt, milo::CMD_PACKET::COMMAND);
    const int target  = at(pkt, milo::CMD_PACKET::TARGET);

    switch (static_cast<COMMAND>(command)) {
    case COMMAND::START_SEQUENCE: handle_start(dest, target); break;
    case COMMAND::PAUSE:          handle_pause(dest);         break;
    case COMMAND::RESUME:         handle_resume(dest);        break;
    case COMMAND::CANCEL:         handle_cancel(dest);        break;
    case COMMAND::ESTOP_ENABLE:   handle_estop(dest);         break;
    default:
        log(std::string("CSV管理では扱わないコマンド ") + command_name(command) + " が届いたので無視しました");
        if (is_bot_id(dest)) reject(dest, static_cast<COMMAND>(command), REJECT_REASON::INVALID_PARAM);
        break;
    }
}

void SequenceEngine::handle_start(int bot_id, int target) {
    const COMMAND req = COMMAND::START_SEQUENCE;
    if (!is_bot_id(bot_id)) {
        log("START_SEQUENCE の宛先は 1〜99 の bot_id にしてください（" + std::to_string(bot_id) + "）");
        return;
    }
    auto& b = bots_[bot_id];

    const auto it = table_.find(target);
    if (it == table_.end() || it->second.empty()) {
        reject(bot_id, req, REJECT_REASON::INVALID_PARAM);
        return;
    }
    if (b.state == SEQUENCE_STATE::RUNNING) { reject(bot_id, req, REJECT_REASON::BUSY); return; }
    if (b.state == SEQUENCE_STATE::SUSPENDED) { reject(bot_id, req, REJECT_REASON::INVALID_STATE); return; }
    if (!b.status.known) { reject(bot_id, req, REJECT_REASON::NOT_READY); return; }  // システム状態が未着
    if (b.status.result == to_int(RESULT::RUNNING)) { reject(bot_id, req, REJECT_REASON::BUSY); return; }

    // 開始してよい Bot の状態：PREPARE は未準備・待機、ほかは待機のみ
    const int s = b.status.bot_state;
    const bool is_prepare = (target == to_int(milo::SEQUENCE_ID::PREPARE));
    const bool idle = (s == to_int(milo::BOT_STATE::IDLE));
    const bool not_ready = (s == to_int(milo::BOT_STATE::NOT_READY));
    if (!(idle || (is_prepare && not_ready))) {
        reject(bot_id, req, not_ready ? REJECT_REASON::NOT_READY : REJECT_REASON::INVALID_STATE);
        return;
    }

    b.state = SEQUENCE_STATE::RUNNING;
    b.sequence_id = target;
    b.index = 0;
    b.stop_code = 0;
    b.step_resumable = true;
    accept(bot_id, req);
    log("Bot " + std::to_string(bot_id) + "：シーケンス " + std::to_string(target) + " を開始");
    send_current_step(bot_id);
}

void SequenceEngine::handle_pause(int bot_id) {
    const COMMAND req = COMMAND::PAUSE;
    if (bot_id == milo::BOT_ID_ALL) {  // 全台：実行中のものだけ中断する
        for (auto& kv : bots_) {
            if (kv.second.state == SEQUENCE_STATE::RUNNING) {
                suspend(kv.first, 0, true);
                accept(kv.first, req);
            }
        }
        return;
    }
    if (!is_bot_id(bot_id)) return;
    if (bots_[bot_id].state != SEQUENCE_STATE::RUNNING) {
        reject(bot_id, req, REJECT_REASON::INVALID_STATE);
        return;
    }
    suspend(bot_id, 0, true);
    accept(bot_id, req);
}

void SequenceEngine::handle_resume(int bot_id) {
    const COMMAND req = COMMAND::RESUME;
    if (!is_bot_id(bot_id)) return;
    auto& b = bots_[bot_id];

    if (b.state != SEQUENCE_STATE::SUSPENDED) { reject(bot_id, req, REJECT_REASON::INVALID_STATE); return; }
    // 中断したステップの結果がまだ返ってきていない（PAUSE 直後など）
    if (b.awaiting) { reject(bot_id, req, REJECT_REASON::BUSY); return; }
    // 再開できないエラーで止まった手順は CANCEL して PREPARE からやり直す
    if (!b.step_resumable) { reject(bot_id, req, REJECT_REASON::INVALID_STATE); return; }

    const int s = b.status.bot_state;
    if (s != to_int(milo::BOT_STATE::IDLE)) {
        REJECT_REASON r = REJECT_REASON::INVALID_STATE;
        if (s == to_int(milo::BOT_STATE::ERROR_))    r = REJECT_REASON::NOT_RESOLVED;  // 先に CLEAR_ERROR
        if (s == to_int(milo::BOT_STATE::NOT_READY)) r = REJECT_REASON::NOT_READY;     // 先に原点出しなど
        reject(bot_id, req, r);
        return;
    }

    b.state = SEQUENCE_STATE::RUNNING;
    b.stop_code = 0;
    accept(bot_id, req);
    log("Bot " + std::to_string(bot_id) + "：ステップ " + std::to_string(b.index + 1) + " から再開");
    send_current_step(bot_id);
}

void SequenceEngine::handle_cancel(int bot_id) {
    const COMMAND req = COMMAND::CANCEL;
    if (!is_bot_id(bot_id)) return;
    auto& b = bots_[bot_id];
    if (b.state != SEQUENCE_STATE::SUSPENDED) { reject(bot_id, req, REJECT_REASON::INVALID_STATE); return; }

    b.state = SEQUENCE_STATE::NONE;
    b.sequence_id = 0;
    b.index = 0;
    b.awaiting = false;
    b.ignore_stale = false;
    b.stop_code = 0;
    b.step_resumable = true;
    accept(bot_id, req);
    log("Bot " + std::to_string(bot_id) + "：シーケンスを取り消し");
}

void SequenceEngine::handle_estop(int bot_id) {
    // 非常停止は必ず受け付ける。実行中の手順を中断中にする（自動では再開しない）
    for (auto& kv : bots_) {
        if (bot_id != milo::BOT_ID_ALL && kv.first != bot_id) continue;
        if (kv.second.state == SEQUENCE_STATE::RUNNING) suspend(kv.first, 0, true);
        accept(kv.first, COMMAND::ESTOP_ENABLE);
    }
    log("非常停止を受信：実行中のシーケンスを中断しました");
}

// -----------------------------------------------------------------------------
// Task_provider のシステム状態
// -----------------------------------------------------------------------------
void SequenceEngine::on_system_state(const Packet& pkt) {
    using P = milo::SYS_STATE_PACKET;
    if (pkt.size() < static_cast<size_t>(to_int(P::DATA_LENGTH))) {
        log("システム状態の配列が短すぎるので無視しました");
        return;
    }
    const int bot_id = at(pkt, P::BOT_ID);
    if (!is_bot_id(bot_id)) return;

    auto& b = bots_[bot_id];
    if (!b.status.known) dirty_.insert(bot_id);  // 初めて見えた Bot も GUI に知らせる
    const bool was_resumable = can_resume(b);

    b.status.known           = true;
    b.status.bot_state       = at(pkt, P::BOT_STATE);
    b.status.running_command = at(pkt, P::RUNNING_COMMAND);
    b.status.result          = at(pkt, P::RESULT);
    b.status.error_code      = at(pkt, P::ERROR_CODE);
    b.status.resumable       = at(pkt, P::RESUMABLE);

    if (b.awaiting) {
        const auto& step = table_.at(b.sequence_id).at(b.index);
        const int result = b.status.result;

        if (b.ignore_stale) {
            const bool same = (b.status.running_command == to_int(step.command) &&
                               result == b.stale_result && b.status.error_code == b.stale_error);
            if (same) return;  // 前回の結果がまだ出ている
            b.ignore_stale = false;
        }

        if (b.status.running_command == to_int(step.command)) {
            if (result == to_int(RESULT::COMPLETED)) {
                advance(bot_id);
            } else if (result == to_int(RESULT::FAILED)) {
                log("Bot " + std::to_string(bot_id) + "：" + command_name(to_int(step.command)) +
                    " が失敗（エラー " + std::to_string(b.status.error_code) + "）");
                suspend(bot_id, b.status.error_code, b.status.resumable != 0);
                b.awaiting = false;
            } else if (result == to_int(RESULT::REJECTED)) {
                log("Bot " + std::to_string(bot_id) + "：" + command_name(to_int(step.command)) +
                    " が拒否された（理由 " + std::to_string(b.status.error_code) + "）");
                suspend(bot_id, b.status.error_code, true);
                b.awaiting = false;
            } else if (result == to_int(RESULT::ABORTED)) {
                suspend(bot_id, b.status.error_code, true);
                b.awaiting = false;
            }
        }
    }

    if (can_resume(b) != was_resumable) dirty_.insert(bot_id);
}

// -----------------------------------------------------------------------------
// 内部処理
// -----------------------------------------------------------------------------
void SequenceEngine::send_current_step(int bot_id) {
    auto& b = bots_[bot_id];
    const auto& step = table_.at(b.sequence_id).at(b.index);

    Packet p(static_cast<size_t>(to_int(milo::CMD_PACKET::DATA_LENGTH)), 0);
    p[to_int(milo::CMD_PACKET::DEST_ID)] = bot_id;
    p[to_int(milo::CMD_PACKET::COMMAND)] = to_int(step.command);
    p[to_int(milo::CMD_PACKET::TARGET)]  = step.target;
    p[to_int(milo::CMD_PACKET::PARAM_1)] = step.param;
    commands_.push_back(p);

    b.awaiting = true;
    // 今見えている状態が「同じコマンドの終わった結果」なら、それは前回の結果
    b.ignore_stale = (b.status.running_command == to_int(step.command) && is_finished(b.status.result));
    b.stale_result = b.status.result;
    b.stale_error  = b.status.error_code;
    dirty_.insert(bot_id);
    log("Bot " + std::to_string(bot_id) + "：ステップ " + std::to_string(b.index + 1) + "/" +
        std::to_string(table_.at(b.sequence_id).size()) + " " + command_name(to_int(step.command)) +
        "（対象 " + std::to_string(step.target) + "）を送信");
}

void SequenceEngine::advance(int bot_id) {
    auto& b = bots_[bot_id];
    b.awaiting = false;
    b.ignore_stale = false;
    ++b.index;
    dirty_.insert(bot_id);

    if (b.index >= table_.at(b.sequence_id).size()) {
        log("Bot " + std::to_string(bot_id) + "：シーケンス " + std::to_string(b.sequence_id) + " が完了");
        b.state = SEQUENCE_STATE::NONE;
        b.sequence_id = 0;
        b.index = 0;
        return;
    }
    // 中断中（PAUSE と同時にステップが終わった場合など）は次を送らず、次のステップで止めておく
    if (b.state == SEQUENCE_STATE::RUNNING) send_current_step(bot_id);
}

void SequenceEngine::suspend(int bot_id, int stop_code, bool resumable) {
    auto& b = bots_[bot_id];
    if (b.state == SEQUENCE_STATE::NONE) return;
    b.state = SEQUENCE_STATE::SUSPENDED;
    // 送信済みのステップの結果（中断・完了）は引き続き待つので awaiting はここでは変えない
    if (stop_code != 0 || b.stop_code == 0) b.stop_code = stop_code;
    b.step_resumable = resumable;
    dirty_.insert(bot_id);
}

void SequenceEngine::accept(int bot_id, COMMAND request) {
    auto& b = bots_[bot_id];
    b.last_request  = to_int(request);
    b.last_result   = to_int(RESULT::COMPLETED);
    b.reject_reason = 0;
    dirty_.insert(bot_id);
}

void SequenceEngine::reject(int bot_id, COMMAND request, REJECT_REASON reason) {
    auto& b = bots_[bot_id];
    b.last_request  = to_int(request);
    b.last_result   = to_int(RESULT::REJECTED);
    b.reject_reason = to_int(reason);
    dirty_.insert(bot_id);
    log("Bot " + std::to_string(bot_id) + "：" + command_name(to_int(request)) + " を拒否（理由 " +
        std::to_string(to_int(reason)) + "）");
}

bool SequenceEngine::can_resume(const BotSequence& b) const {
    return b.state == SEQUENCE_STATE::SUSPENDED && !b.awaiting && b.step_resumable &&
           b.status.bot_state == to_int(milo::BOT_STATE::IDLE);
}

Packet SequenceEngine::make_state(int bot_id, const BotSequence& b) const {
    using P = milo::SEQ_STATE_PACKET;
    Packet p(static_cast<size_t>(to_int(P::DATA_LENGTH)), 0);
    const bool active = (b.state != SEQUENCE_STATE::NONE);
    const auto* steps = active ? &table_.at(b.sequence_id) : nullptr;

    p[to_int(P::BOT_ID)]        = bot_id;
    p[to_int(P::SEQ_STATE)]     = to_int(b.state);
    p[to_int(P::SEQUENCE_ID)]   = active ? b.sequence_id : 0;
    p[to_int(P::STEP)]          = active ? static_cast<int32_t>(b.index + 1) : 0;
    p[to_int(P::STEP_COUNT)]    = active ? static_cast<int32_t>(steps->size()) : 0;
    p[to_int(P::STEP_COMMAND)]  = active ? to_int(steps->at(b.index).command) : -1;
    p[to_int(P::STOP_CODE)]     = b.state == SEQUENCE_STATE::SUSPENDED ? b.stop_code : 0;
    p[to_int(P::RESUMABLE)]     = can_resume(b) ? 1 : 0;
    p[to_int(P::LAST_REQUEST)]  = b.last_request;
    p[to_int(P::LAST_RESULT)]   = b.last_result;
    p[to_int(P::REJECT_REASON)] = b.reject_reason;
    return p;
}

// -----------------------------------------------------------------------------
// 出力
// -----------------------------------------------------------------------------
std::vector<Packet> SequenceEngine::take_commands() {
    std::vector<Packet> out;
    out.swap(commands_);
    return out;
}

std::vector<Packet> SequenceEngine::take_changed_states() {
    std::vector<Packet> out;
    for (int id : dirty_) out.push_back(make_state(id, bots_.at(id)));
    dirty_.clear();
    return out;
}

std::vector<Packet> SequenceEngine::all_states() const {
    std::vector<Packet> out;
    for (const auto& kv : bots_) out.push_back(make_state(kv.first, kv.second));
    return out;
}

std::vector<std::string> SequenceEngine::take_logs() {
    std::vector<std::string> out;
    out.swap(logs_);
    return out;
}

milo::SEQUENCE_STATE SequenceEngine::sequence_state(int bot_id) const {
    const auto it = bots_.find(bot_id);
    return it == bots_.end() ? SEQUENCE_STATE::NONE : it->second.state;
}

int SequenceEngine::current_step(int bot_id) const {
    const auto it = bots_.find(bot_id);
    if (it == bots_.end() || it->second.state == SEQUENCE_STATE::NONE) return 0;
    return static_cast<int>(it->second.index) + 1;
}

}  // namespace seqmgr
