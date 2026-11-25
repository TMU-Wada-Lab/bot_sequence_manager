// -*- C++ -*-
/*!
 * @file  FakeTaskProvider.cpp
 * @brief テスト用の偽 Task_provider の中身
 */
#include "FakeTaskProvider.h"

#include "bot_command_def.h"

namespace fake_tp {

using milo::BOT_STATE;
using milo::COMMAND;
using milo::RESULT;
using milo::to_int;

FakeTaskProvider::FakeTaskProvider(const Settings& s) : settings_(s) {
    for (int id : settings_.bot_ids) {
        Bot b;
        b.state = settings_.initial_state;
        bots_[id] = b;
    }
}

void FakeTaskProvider::finish(Bot& b, int result, int error_code) {
    b.busy = false;
    b.result = result;
    b.error_code = error_code;
}

void FakeTaskProvider::on_command(const Packet& cmd, int64_t now_ms) {
    if (cmd.size() < 2) return;
    const int dest = cmd[to_int(milo::CMD_PACKET::DEST_ID)];
    const int command = cmd[to_int(milo::CMD_PACKET::COMMAND)];

    if (dest == milo::BOT_ID_ALL) {
        for (auto& kv : bots_) command_for_bot(kv.first, kv.second, command, now_ms);
        return;
    }
    const auto it = bots_.find(dest);
    if (it == bots_.end()) {
        log("登録されていない Bot " + std::to_string(dest) + " 宛てのコマンドを無視しました");
        return;
    }
    command_for_bot(dest, it->second, command, now_ms);
}

void FakeTaskProvider::command_for_bot(int bot_id, Bot& b, int cmd, int64_t now_ms) {
    const std::string who = "Bot " + std::to_string(bot_id) + "：";

    switch (static_cast<COMMAND>(cmd)) {
    case COMMAND::PAUSE:
        if (b.busy) {
            finish(b, to_int(RESULT::ABORTED));
            b.state = to_int(BOT_STATE::IDLE);
            log(who + "PAUSE で中断");
        }
        return;
    case COMMAND::ESTOP_ENABLE:
        if (b.busy) finish(b, to_int(RESULT::ABORTED));
        b.state = to_int(BOT_STATE::ESTOP);
        log(who + "非常停止");
        return;
    case COMMAND::ESTOP_DISENABLE:
        if (b.state == to_int(BOT_STATE::ESTOP)) {
            b.state = to_int(BOT_STATE::NOT_READY);  // 原点出しをやり直す
            b.command = cmd;
            finish(b, to_int(RESULT::COMPLETED));
            log(who + "非常停止解除 → 未準備");
        }
        return;
    case COMMAND::CLEAR_ERROR:
        if (b.state == to_int(BOT_STATE::ERROR_)) {
            b.state = to_int(BOT_STATE::IDLE);
            b.command = cmd;
            b.resumable = 0;
            finish(b, to_int(RESULT::COMPLETED));
            log(who + "エラー解除 → 待機");
        }
        return;
    default:
        break;
    }

    if (b.busy) {
        log(who + "実行中なので " + std::to_string(cmd) + " を無視しました");
        return;
    }
    if (b.state == to_int(BOT_STATE::ESTOP) || b.state == to_int(BOT_STATE::ERROR_)) {
        b.command = cmd;
        finish(b, to_int(RESULT::REJECTED), to_int(milo::REJECT_REASON::INVALID_STATE));
        log(who + std::to_string(cmd) + " を拒否（非常停止中またはエラー中）");
        return;
    }

    // 失敗のまねごと（1 回だけ）
    if (!failed_once_ && cmd == settings_.fail_command) {
        failed_once_ = true;
        b.command = cmd;
        if (settings_.fail_mode == 2) {
            finish(b, to_int(RESULT::REJECTED), settings_.fail_code);
            log(who + std::to_string(cmd) + " を拒否（テスト）");
        } else {
            b.state = to_int(BOT_STATE::ERROR_);
            b.resumable = settings_.fail_resumable;
            finish(b, to_int(RESULT::FAILED), settings_.fail_code);
            log(who + std::to_string(cmd) + " を失敗（テスト）");
        }
        return;
    }

    b.command = cmd;
    b.result = to_int(RESULT::RUNNING);
    b.error_code = 0;
    b.busy = true;
    b.done_at = now_ms + settings_.step_time_ms;
    if (b.state != to_int(BOT_STATE::NOT_READY)) b.state = to_int(BOT_STATE::RUNNING);
    log(who + std::to_string(cmd) + " を実行開始");
}

void FakeTaskProvider::tick(int64_t now_ms) {
    for (auto& kv : bots_) {
        Bot& b = kv.second;
        if (!b.busy || now_ms < b.done_at) continue;
        finish(b, to_int(RESULT::COMPLETED));
        if (b.command == to_int(COMMAND::MECHA_HOME)) {
            b.state = to_int(BOT_STATE::IDLE);  // 原点出しが終われば準備完了
        } else if (b.command == to_int(COMMAND::SHUTDOWN)) {
            b.state = to_int(BOT_STATE::SHUTTING_DOWN);
        } else if (b.state != to_int(BOT_STATE::NOT_READY)) {
            b.state = to_int(BOT_STATE::IDLE);
        }
        log("Bot " + std::to_string(kv.first) + "：" + std::to_string(b.command) + " が完了");
    }
}

std::vector<Packet> FakeTaskProvider::states() const {
    using P = milo::SYS_STATE_PACKET;
    std::vector<Packet> out;
    for (const auto& kv : bots_) {
        const Bot& b = kv.second;
        Packet p(static_cast<size_t>(to_int(P::DATA_LENGTH)), 0);
        p[to_int(P::BOT_ID)]          = kv.first;
        p[to_int(P::BOT_STATE)]       = b.state;
        p[to_int(P::RUNNING_COMMAND)] = b.command;
        p[to_int(P::RESULT)]          = b.result;
        p[to_int(P::ERROR_CODE)]      = b.error_code;
        p[to_int(P::RESUMABLE)]       = b.resumable;
        out.push_back(p);
    }
    return out;
}

std::vector<std::string> FakeTaskProvider::take_logs() {
    std::vector<std::string> out;
    out.swap(logs_);
    return out;
}

}  // namespace fake_tp
