// -*- C++ -*-
/*!
 * @file  sequence_engine_test.cpp
 * @brief CSV管理のロジック（CSV 読み込み・シーケンス状態遷移）の単体テスト
 *
 * OpenRTM なしでビルド・実行できる。失敗した項目があれば終了コード 1。
 */
#include <iostream>
#include <sstream>
#include <string>

#include "SequenceCsv.h"
#include "SequenceEngine.h"

using namespace seqmgr;
using milo::COMMAND;
using milo::RESULT;
using milo::SEQUENCE_STATE;
using milo::to_int;

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (cond) { ++g_passed; }                                                     \
        else { ++g_failed; std::cerr << "  NG " << __LINE__ << ": " #cond << std::endl; } \
    } while (0)

// ---- 補助 ----
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

static SequenceTable table() {
    SequenceTable t;
    std::string err;
    std::istringstream ss(kCsv);
    parse_sequence_csv(ss, t, err);
    return t;
}

static Packet cmd(int bot, COMMAND c, int target = 0) { return Packet{bot, to_int(c), target, 0, 0, 0}; }

static Packet sys(int bot, milo::BOT_STATE s, int running_cmd, RESULT r, int err = 0, int resumable = 0) {
    return Packet{bot, to_int(s), running_cmd, to_int(r), err, resumable, 0, 0, 0, 0};
}
static Packet sys(int bot, milo::BOT_STATE s, COMMAND c, RESULT r, int err = 0, int resumable = 0) {
    return sys(bot, s, to_int(c), r, err, resumable);
}

// 直近のシーケンス状態（bot の最新 1 件）
static Packet last_state(SequenceEngine& e, int bot) {
    Packet found;
    for (const auto& p : e.take_changed_states())
        if (p[0] == bot) found = p;
    return found;
}

static int field(const Packet& p, milo::SEQ_STATE_PACKET f) { return p.at(to_int(f)); }

static const auto IDLE    = milo::BOT_STATE::IDLE;
static const auto RUNNING = milo::BOT_STATE::RUNNING;

// ---- テスト ----
static void test_csv_ok() {
    std::cout << "CSV：正しいファイルを読める" << std::endl;
    SequenceTable t;
    std::string err;
    std::istringstream ss(std::string("\xEF\xBB\xBF") + "sequence,step,command,target,param\r\n"
                          "# コメント行\r\n"
                          "PREPARE,1,SELF_DIAG,SELF,\r\n"
                          "\r\n"
                          "PREPARE,2,LOAD_MAP,1,\r\n");
    CHECK(parse_sequence_csv(ss, t, err));
    CHECK(t[to_int(milo::SEQUENCE_ID::PREPARE)].size() == 2);
    CHECK(t[to_int(milo::SEQUENCE_ID::PREPARE)][1].target == 1);

    SequenceTable full = table();
    CHECK(full[1].size() == 3 && full[2].size() == 5 && full[3].size() == 2);
    CHECK(full[2][1].command == COMMAND::MOVE && full[2][1].target == 102);
}

static void test_csv_errors() {
    std::cout << "CSV：書き間違いをエラーにする" << std::endl;
    const char* bad[] = {
        "seq,step,command,target\nPREPARE,1,SELF_DIAG,SELF\n",                          // 見出し
        "sequence,step,command,target,param\nPREPARE,1,ESTOP_ENABLE,SELF,\n",          // 書けないコマンド
        "sequence,step,command,target,param\nPREPARE,1,SELF_DAIG,SELF,\n",             // 綴り
        "sequence,step,command,target,param\nNORMAL,1,MOVE,STORE9,\n",                 // 地点名
        "sequence,step,command,target,param\nFOO,1,MOVE,HOME,\n",                      // シーケンス名
        "sequence,step,command,target,param\nNORMAL,1,MOVE,HOME,\nNORMAL,3,MOVE,HOME,\n",  // 番号飛び
        "sequence,step,command,target,param\nNORMAL,1,MOVE,HOME,x\n",                  // param
        "",                                                                            // 空
    };
    for (const char* text : bad) {
        SequenceTable t;
        std::string err;
        std::istringstream ss(text);
        CHECK(!parse_sequence_csv(ss, t, err));
        CHECK(!err.empty());
    }
}

static void test_start_rejects() {
    std::cout << "START_SEQUENCE：開始できない条件で拒否する" << std::endl;
    SequenceEngine e(table());
    using P = milo::SEQ_STATE_PACKET;

    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 2));  // システム状態がまだ来ていない
    auto s = last_state(e, 1);
    CHECK(field(s, P::LAST_RESULT) == to_int(RESULT::REJECTED));
    CHECK(field(s, P::REJECT_REASON) == to_int(milo::REJECT_REASON::NOT_READY));

    e.on_system_state(sys(1, milo::BOT_STATE::NOT_READY, -1, RESULT::COMPLETED));
    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 2));  // 未準備で NORMAL
    s = last_state(e, 1);
    CHECK(field(s, P::REJECT_REASON) == to_int(milo::REJECT_REASON::NOT_READY));

    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 9));  // ないシーケンス
    s = last_state(e, 1);
    CHECK(field(s, P::REJECT_REASON) == to_int(milo::REJECT_REASON::INVALID_PARAM));

    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 1));  // 未準備でも PREPARE は開始できる
    s = last_state(e, 1);
    CHECK(field(s, P::LAST_RESULT) == to_int(RESULT::COMPLETED));
    CHECK(e.sequence_state(1) == SEQUENCE_STATE::RUNNING);
    const auto c = e.take_commands();
    CHECK(c.size() == 1 && c[0] == cmd(1, COMMAND::SELF_DIAG, 0));

    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 1));  // 実行中に二重開始
    s = last_state(e, 1);
    CHECK(field(s, P::REJECT_REASON) == to_int(milo::REJECT_REASON::BUSY));
}

static void test_normal_run() {
    std::cout << "NORMAL：5 ステップを順に送り、完了でなしに戻る" << std::endl;
    SequenceEngine e(table());
    e.on_system_state(sys(1, IDLE, -1, RESULT::COMPLETED));
    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 2));

    const Packet expected[] = {cmd(1, COMMAND::RECEIVE, 101), cmd(1, COMMAND::MOVE, 102),
                               cmd(1, COMMAND::EXCHANGE, 102), cmd(1, COMMAND::MOVE, 101),
                               cmd(1, COMMAND::SEND_OUT, 101)};
    for (int i = 0; i < 5; ++i) {
        auto c = e.take_commands();
        CHECK(c.size() == 1 && c[0] == expected[i]);
        CHECK(e.current_step(1) == i + 1);
        const COMMAND sc = static_cast<COMMAND>(expected[i][1]);
        e.on_system_state(sys(1, RUNNING, sc, RESULT::RUNNING));
        CHECK(e.take_commands().empty());  // 実行中は次を送らない
        e.on_system_state(sys(1, IDLE, sc, RESULT::COMPLETED));
    }
    CHECK(e.take_commands().empty());
    CHECK(e.sequence_state(1) == SEQUENCE_STATE::NONE);
}

static void test_stale_result() {
    std::cout << "前回の同じコマンドの完了を、今回の完了と取り違えない" << std::endl;
    SequenceEngine e(table());
    // 直前に MOVE(HOME) を単発で実行して完了していた
    e.on_system_state(sys(1, IDLE, COMMAND::MOVE, RESULT::COMPLETED));
    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 3));  // END：MOVE → SHUTDOWN
    CHECK(e.take_commands().size() == 1);

    e.on_system_state(sys(1, IDLE, COMMAND::MOVE, RESULT::COMPLETED));  // 前回の結果がまだ出ている
    CHECK(e.take_commands().empty());
    CHECK(e.current_step(1) == 1);

    e.on_system_state(sys(1, RUNNING, COMMAND::MOVE, RESULT::RUNNING));
    e.on_system_state(sys(1, IDLE, COMMAND::MOVE, RESULT::COMPLETED));
    auto c = e.take_commands();
    CHECK(c.size() == 1 && c[0] == cmd(1, COMMAND::SHUTDOWN, 0));
}

static void test_pause_resume() {
    std::cout << "PAUSE → 中断中 → RESUME で止まったステップを再送" << std::endl;
    using P = milo::SEQ_STATE_PACKET;
    SequenceEngine e(table());
    e.on_system_state(sys(1, IDLE, -1, RESULT::COMPLETED));
    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 2));
    e.take_commands();
    e.on_system_state(sys(1, IDLE, COMMAND::RECEIVE, RESULT::COMPLETED));
    e.take_commands();  // MOVE(STORE1)
    e.on_system_state(sys(1, RUNNING, COMMAND::MOVE, RESULT::RUNNING));

    e.on_seq_command(cmd(1, COMMAND::PAUSE));
    CHECK(e.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);
    auto s = last_state(e, 1);
    CHECK(field(s, P::STEP) == 2 && field(s, P::RESUMABLE) == 0);

    e.on_seq_command(cmd(1, COMMAND::RESUME));  // まだ中断の結果が来ていない
    s = last_state(e, 1);
    CHECK(field(s, P::REJECT_REASON) == to_int(milo::REJECT_REASON::BUSY));

    e.on_system_state(sys(1, IDLE, COMMAND::MOVE, RESULT::ABORTED));
    s = last_state(e, 1);
    CHECK(field(s, P::RESUMABLE) == 1);

    e.on_seq_command(cmd(1, COMMAND::RESUME));
    auto c = e.take_commands();
    CHECK(c.size() == 1 && c[0] == cmd(1, COMMAND::MOVE, 102));
    CHECK(e.sequence_state(1) == SEQUENCE_STATE::RUNNING);

    // 再送後、前回の ABORTED が残っていても取り違えない
    e.on_system_state(sys(1, IDLE, COMMAND::MOVE, RESULT::ABORTED));
    CHECK(e.sequence_state(1) == SEQUENCE_STATE::RUNNING);
    e.on_system_state(sys(1, RUNNING, COMMAND::MOVE, RESULT::RUNNING));
    e.on_system_state(sys(1, IDLE, COMMAND::MOVE, RESULT::COMPLETED));
    c = e.take_commands();
    CHECK(c.size() == 1 && c[0] == cmd(1, COMMAND::EXCHANGE, 102));
}

static void test_pause_while_completing() {
    std::cout << "PAUSE と同時にステップが完了したら、次のステップで止めておく" << std::endl;
    SequenceEngine e(table());
    e.on_system_state(sys(1, IDLE, -1, RESULT::COMPLETED));
    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 2));
    e.take_commands();
    e.on_seq_command(cmd(1, COMMAND::PAUSE));
    e.on_system_state(sys(1, IDLE, COMMAND::RECEIVE, RESULT::COMPLETED));
    CHECK(e.take_commands().empty());
    CHECK(e.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);
    CHECK(e.current_step(1) == 2);
    e.on_seq_command(cmd(1, COMMAND::RESUME));
    auto c = e.take_commands();
    CHECK(c.size() == 1 && c[0] == cmd(1, COMMAND::MOVE, 102));
}

static void test_error_recovery() {
    std::cout << "エラー：再開可は CLEAR_ERROR 後に RESUME、再開不可は CANCEL のみ" << std::endl;
    using P = milo::SEQ_STATE_PACKET;
    const int E001 = to_int(milo::ERROR_CODE::E001_POSE_LOST);
    const int E003 = to_int(milo::ERROR_CODE::E003_EXCHANGE_FAILED);

    // 再開できる（E-001）
    SequenceEngine e(table());
    e.on_system_state(sys(1, IDLE, -1, RESULT::COMPLETED));
    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 2));
    e.take_commands();
    e.on_system_state(sys(1, milo::BOT_STATE::ERROR_, COMMAND::RECEIVE, RESULT::FAILED, E001, 1));
    CHECK(e.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);
    auto s = last_state(e, 1);
    CHECK(field(s, P::STOP_CODE) == E001);

    e.on_seq_command(cmd(1, COMMAND::RESUME));  // まだエラー状態
    s = last_state(e, 1);
    CHECK(field(s, P::REJECT_REASON) == to_int(milo::REJECT_REASON::NOT_RESOLVED));

    e.on_system_state(sys(1, IDLE, COMMAND::CLEAR_ERROR, RESULT::COMPLETED));  // CLEAR_ERROR 後
    e.on_seq_command(cmd(1, COMMAND::RESUME));
    auto c = e.take_commands();
    CHECK(c.size() == 1 && c[0] == cmd(1, COMMAND::RECEIVE, 101));

    // 再開できない（E-003）
    SequenceEngine f(table());
    f.on_system_state(sys(1, IDLE, -1, RESULT::COMPLETED));
    f.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 2));
    f.take_commands();
    f.on_system_state(sys(1, milo::BOT_STATE::ERROR_, COMMAND::RECEIVE, RESULT::FAILED, E003, 0));
    f.on_system_state(sys(1, IDLE, COMMAND::CLEAR_ERROR, RESULT::COMPLETED));
    f.on_seq_command(cmd(1, COMMAND::RESUME));
    s = last_state(f, 1);
    CHECK(field(s, P::LAST_RESULT) == to_int(RESULT::REJECTED));
    CHECK(f.take_commands().empty());
    f.on_seq_command(cmd(1, COMMAND::CANCEL));
    CHECK(f.sequence_state(1) == SEQUENCE_STATE::NONE);
}

static void test_rejected_step() {
    std::cout << "ステップが拒否されたら中断中にし、理由を知らせる" << std::endl;
    using P = milo::SEQ_STATE_PACKET;
    const int NOT_AT_START = to_int(milo::REJECT_REASON::NOT_AT_START);
    SequenceEngine e(table());
    e.on_system_state(sys(1, IDLE, -1, RESULT::COMPLETED));
    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 2));
    e.take_commands();
    e.on_system_state(sys(1, IDLE, COMMAND::RECEIVE, RESULT::REJECTED, NOT_AT_START));
    CHECK(e.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);
    auto s = last_state(e, 1);
    CHECK(field(s, P::STOP_CODE) == NOT_AT_START);
    e.on_seq_command(cmd(1, COMMAND::CANCEL));
    CHECK(e.sequence_state(1) == SEQUENCE_STATE::NONE);
}

static void test_estop_and_multi_bot() {
    std::cout << "ESTOP・PAUSE(0)：全台の実行中シーケンスを中断。Bot ごとに独立" << std::endl;
    SequenceEngine e(table());
    e.on_system_state(sys(1, IDLE, -1, RESULT::COMPLETED));
    e.on_system_state(sys(2, IDLE, -1, RESULT::COMPLETED));
    e.on_system_state(sys(3, IDLE, -1, RESULT::COMPLETED));
    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 2));
    e.on_seq_command(cmd(2, COMMAND::START_SEQUENCE, 3));
    auto c = e.take_commands();
    CHECK(c.size() == 2 && c[0][0] == 1 && c[1][0] == 2);

    // Bot 2 だけ進む
    e.on_system_state(sys(2, IDLE, COMMAND::MOVE, RESULT::COMPLETED));
    c = e.take_commands();
    CHECK(c.size() == 1 && c[0] == cmd(2, COMMAND::SHUTDOWN, 0));
    CHECK(e.current_step(1) == 1);

    e.on_seq_command(cmd(0, COMMAND::ESTOP_ENABLE));
    CHECK(e.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);
    CHECK(e.sequence_state(2) == SEQUENCE_STATE::SUSPENDED);
    CHECK(e.sequence_state(3) == SEQUENCE_STATE::NONE);

    SequenceEngine g(table());
    g.on_system_state(sys(1, IDLE, -1, RESULT::COMPLETED));
    g.on_system_state(sys(2, IDLE, -1, RESULT::COMPLETED));
    g.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 2));
    g.on_seq_command(cmd(0, COMMAND::PAUSE));
    CHECK(g.sequence_state(1) == SEQUENCE_STATE::SUSPENDED);
    CHECK(g.sequence_state(2) == SEQUENCE_STATE::NONE);
}

static void test_state_packet() {
    std::cout << "シーケンス状態の配列の中身" << std::endl;
    using P = milo::SEQ_STATE_PACKET;
    SequenceEngine e(table());
    e.on_system_state(sys(1, IDLE, -1, RESULT::COMPLETED));
    e.on_seq_command(cmd(1, COMMAND::START_SEQUENCE, 2));
    auto s = last_state(e, 1);
    CHECK(s.size() == static_cast<size_t>(to_int(P::DATA_LENGTH)));
    CHECK(field(s, P::BOT_ID) == 1);
    CHECK(field(s, P::SEQ_STATE) == to_int(SEQUENCE_STATE::RUNNING));
    CHECK(field(s, P::SEQUENCE_ID) == 2);
    CHECK(field(s, P::STEP) == 1 && field(s, P::STEP_COUNT) == 5);
    CHECK(field(s, P::STEP_COMMAND) == to_int(COMMAND::RECEIVE));
    CHECK(field(s, P::LAST_REQUEST) == to_int(COMMAND::START_SEQUENCE));
    CHECK(field(s, P::LAST_RESULT) == to_int(RESULT::COMPLETED));
    CHECK(e.all_states().size() == 1);
}

int main() {
    test_csv_ok();
    test_csv_errors();
    test_start_rejects();
    test_normal_run();
    test_stale_result();
    test_pause_resume();
    test_pause_while_completing();
    test_error_recovery();
    test_rejected_step();
    test_estop_and_multi_bot();
    test_state_packet();
    std::cout << std::endl << "合格 " << g_passed << " / 不合格 " << g_failed << std::endl;
    return g_failed == 0 ? 0 : 1;
}
