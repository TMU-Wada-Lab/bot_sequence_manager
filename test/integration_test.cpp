// -*- C++ -*-
/*!
 * @file  integration_test.cpp
 * @brief CSV管理のロジック ⇄ 偽 Task_provider を、時間を進めながらつないで動かす結合テスト
 *
 * RTC のポートの代わりに、配列を直接受け渡す（OpenRTM なしで実行できる）。
 * GUI は中継するだけなので省略している。
 */
#include <iostream>
#include <sstream>
#include <string>

#include "FakeTaskProvider.h"
#include "SequenceCsv.h"
#include "SequenceEngine.h"

using milo::COMMAND;
using milo::SEQUENCE_STATE;
using milo::to_int;

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (cond) { ++g_passed; }                                                     \
        else { ++g_failed; std::cerr << "  NG " << __LINE__ << ": " #cond << std::endl; } \
    } while (0)

static const char* kCsv =
    "sequence,step,command,target,param\n"
    "PREPARE,1,SELF_DIAG,SELF,\n"
    "PREPARE,2,LOAD_MAP,1,\n"
    "PREPARE,3,MECHA_HOME,SELF,\n"
    "NORMAL,1,RECEIVE,HOME,\n"
    "NORMAL,2,MOVE,STORE1,\n"
    "NORMAL,3,EXCHANGE,STORE1,\n"
    "NORMAL,4,MOVE,HOME,\n"
    "NORMAL,5,SEND_OUT,HOME,\n"
    "END,1,MOVE,HOME,\n"
    "END,2,SHUTDOWN,SELF,\n";

// CSV管理 ⇄ 偽 Task_provider をつないだもの
struct Rig {
    seqmgr::SequenceEngine engine;
    fake_tp::FakeTaskProvider tp;
    int64_t now = 0;
    std::vector<seqmgr::Packet> sent;  // CSV管理が送ったコマンド（記録）

    static seqmgr::SequenceTable table() {
        seqmgr::SequenceTable t;
        std::string err;
        std::istringstream ss(kCsv);
        seqmgr::parse_sequence_csv(ss, t, err);
        return t;
    }
    explicit Rig(const fake_tp::Settings& s) : engine(table()), tp(s) { step(0); }

    // 50 ms 周期で ms だけ進める（RTC の onExecute 相当）
    void step(int ms) {
        const int64_t end = now + ms;
        do {
            tp.tick(now);
            for (const auto& st : tp.states()) engine.on_system_state(st);
            for (const auto& c : engine.take_commands()) {
                sent.push_back(c);
                tp.on_command(c, now);
            }
            engine.take_changed_states();
            engine.take_logs();
            tp.take_logs();
            now += 50;
        } while (now < end);
    }
    void gui(int bot, COMMAND c, int target = 0) {
        const seqmgr::Packet p{bot, to_int(c), target, 0, 0, 0};
        engine.on_seq_command(p);
        // PAUSE・ESTOP は GUI が Task_provider にも送る
        if (c == COMMAND::PAUSE || c == COMMAND::ESTOP_ENABLE) tp.on_command(p, now);
    }
    void tp_direct(int bot, COMMAND c) { tp.on_command({bot, to_int(c), 0, 0, 0, 0}, now); }
};

static fake_tp::Settings settings(int initial_state = 1) {
    fake_tp::Settings s;
    s.bot_ids = {1, 2};
    s.initial_state = initial_state;
    s.step_time_ms = 1000;
    return s;
}

static void test_prepare_then_normal() {
    std::cout << "PREPARE（未準備から）→ NORMAL を最後まで実行" << std::endl;
    Rig r(settings(0));
    r.gui(1, COMMAND::START_SEQUENCE, 1);
    r.step(5000);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::NONE);
    CHECK(r.sent.size() == 3);

    r.gui(1, COMMAND::START_SEQUENCE, 2);  // MECHA_HOME の完了で待機になっている
    r.step(7000);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::NONE);
    CHECK(r.sent.size() == 8);
    const int order[] = {0, 1, 2, 7, 8, 9, 8, 10};
    for (int i = 0; i < 8; ++i) CHECK(r.sent[i][1] == order[i]);
}

static void test_pause_resume() {
    std::cout << "NORMAL の途中で PAUSE → RESUME" << std::endl;
    Rig r(settings());
    r.gui(1, COMMAND::START_SEQUENCE, 2);
    r.step(1500);  // RECEIVE 完了、MOVE 実行中
    r.gui(1, COMMAND::PAUSE);
    r.step(500);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);
    CHECK(r.engine.current_step(1) == 2);
    r.step(3000);  // 止まったまま
    CHECK(r.sent.size() == 2);

    r.gui(1, COMMAND::RESUME);
    r.step(6000);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::NONE);
    CHECK(r.sent.size() == 6);  // MOVE を再送したので 5＋1
    CHECK(r.sent[2][1] == to_int(COMMAND::MOVE) && r.sent[2][2] == 102);
}

static void test_failure_recovery() {
    std::cout << "EXCHANGE で失敗 → CLEAR_ERROR → RESUME" << std::endl;
    auto s = settings();
    s.fail_command = to_int(COMMAND::EXCHANGE);
    s.fail_code = to_int(milo::ERROR_CODE::E002_STATION_NOT_FOUND);
    s.fail_resumable = 1;
    Rig r(s);
    r.gui(1, COMMAND::START_SEQUENCE, 2);
    r.step(3000);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);
    CHECK(r.engine.current_step(1) == 3);

    r.gui(1, COMMAND::RESUME);  // エラー中は拒否
    r.step(200);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);

    r.tp_direct(1, COMMAND::CLEAR_ERROR);  // GUI から Task_provider へ直接
    r.step(200);
    r.gui(1, COMMAND::RESUME);
    r.step(4000);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::NONE);
}

static void test_non_resumable() {
    std::cout << "再開できない失敗 → RESUME は拒否、CANCEL で取り消し" << std::endl;
    auto s = settings();
    s.fail_command = to_int(COMMAND::EXCHANGE);
    s.fail_code = to_int(milo::ERROR_CODE::E003_EXCHANGE_FAILED);
    s.fail_resumable = 0;
    Rig r(s);
    r.gui(1, COMMAND::START_SEQUENCE, 2);
    r.step(3000);
    r.tp_direct(1, COMMAND::CLEAR_ERROR);
    r.step(200);
    const size_t before = r.sent.size();
    r.gui(1, COMMAND::RESUME);
    r.step(2000);
    CHECK(r.sent.size() == before);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);
    r.gui(1, COMMAND::CANCEL);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::NONE);
}

static void test_estop_two_bots() {
    std::cout << "2 台で同時に実行 → 非常停止で両方中断 → 解除・原点出し後に RESUME" << std::endl;
    Rig r(settings());
    r.gui(1, COMMAND::START_SEQUENCE, 2);
    r.gui(2, COMMAND::START_SEQUENCE, 3);
    r.step(500);
    r.gui(0, COMMAND::ESTOP_ENABLE);
    r.step(500);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);
    CHECK(r.engine.sequence_state(2) == SEQUENCE_STATE::SUSPENDED);

    r.gui(1, COMMAND::RESUME);  // 非常停止中は拒否
    r.step(200);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);

    r.tp_direct(0, COMMAND::ESTOP_DISENABLE);  // → 未準備
    r.step(200);
    r.gui(1, COMMAND::RESUME);  // 未準備なので拒否
    r.step(200);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);

    r.tp_direct(1, COMMAND::MECHA_HOME);
    r.tp_direct(2, COMMAND::MECHA_HOME);
    r.step(1500);
    r.gui(1, COMMAND::RESUME);
    r.gui(2, COMMAND::RESUME);
    r.step(7000);
    CHECK(r.engine.sequence_state(1) == SEQUENCE_STATE::NONE);
    CHECK(r.engine.sequence_state(2) == SEQUENCE_STATE::NONE);
}

int main() {
    test_prepare_then_normal();
    test_pause_resume();
    test_failure_recovery();
    test_non_resumable();
    test_estop_two_bots();
    std::cout << std::endl << "合格 " << g_passed << " / 不合格 " << g_failed << std::endl;
    return g_failed == 0 ? 0 : 1;
}
