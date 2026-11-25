// -*- C++ -*-
/*!
 * @file  bot_sequence_manager.cpp
 * @brief CSV管理 RTC
 */
#include "bot_sequence_manager.h"

#include <iostream>

// clang-format off
#if RTM_MAJOR_VERSION >= 2
static const char* const bot_sequence_manager_spec[] =
#else
static const char* bot_sequence_manager_spec[] =
#endif
  {
    "implementation_id", "bot_sequence_manager",
    "type_name",         "bot_sequence_manager",
    "description",       "MiLo Bot sequence (CSV) manager",
    "version",           "1.0.0",
    "vendor",            "TMU-Wada-Lab",
    "category",          "MiLoBot",
    "activity_type",     "PERIODIC",
    "kind",              "DataFlowComponent",
    "max_instance",      "1",
    "language",          "C++",
    "lang_type",         "compile",
    "conf.default.csv_file",        "../bot_system_def/bot_sequence.csv",
    "conf.default.state_period_ms", "500",
    "conf.__widget__.csv_file",        "text",
    "conf.__widget__.state_period_ms", "text",
    "conf.__type__.csv_file",        "string",
    "conf.__type__.state_period_ms", "int",
    ""
  };
// clang-format on

bot_sequence_manager::bot_sequence_manager(RTC::Manager* manager)
    : RTC::DataFlowComponentBase(manager),
      m_state_period_ms(500),
      m_seq_cmdIn("seq_cmd", m_seq_cmd),
      m_sys_stateIn("sys_state", m_sys_state),
      m_cmdOut("cmd", m_cmd),
      m_seq_stateOut("seq_state", m_seq_state) {}

bot_sequence_manager::~bot_sequence_manager() {}

RTC::ReturnCode_t bot_sequence_manager::onInitialize() {
    addInPort("seq_cmd", m_seq_cmdIn);
    addInPort("sys_state", m_sys_stateIn);
    addOutPort("cmd", m_cmdOut);
    addOutPort("seq_state", m_seq_stateOut);

    bindParameter("csv_file", m_csv_file, "../bot_system_def/bot_sequence.csv");
    bindParameter("state_period_ms", m_state_period_ms, "500");
    return RTC::RTC_OK;
}

RTC::ReturnCode_t bot_sequence_manager::onActivated(RTC::UniqueId /*ec_id*/) {
    // 起動（アクティブ化）のたびに CSV を読み直す。書き間違いがあればエラー状態にする。
    seqmgr::SequenceTable table;
    std::string error;
    if (!seqmgr::load_sequence_csv(m_csv_file, table, error)) {
        std::cerr << "[bot_sequence_manager] CSV の読み込みに失敗: " << error << std::endl;
        return RTC::RTC_ERROR;
    }
    std::cout << "[bot_sequence_manager] CSV を読み込みました: " << m_csv_file << std::endl;
    for (const auto& kv : table) {
        std::cout << "  シーケンス " << kv.first << "：" << kv.second.size() << " ステップ" << std::endl;
    }

    m_engine.reset(new seqmgr::SequenceEngine(std::move(table)));
    m_last_state_sent = std::chrono::steady_clock::now();
    return RTC::RTC_OK;
}

RTC::ReturnCode_t bot_sequence_manager::onDeactivated(RTC::UniqueId /*ec_id*/) {
    m_engine.reset();  // 実行中・中断中の手順は破棄される
    return RTC::RTC_OK;
}

RTC::ReturnCode_t bot_sequence_manager::onExecute(RTC::UniqueId /*ec_id*/) {
    if (!m_engine) return RTC::RTC_OK;

    // 先にシステム状態を反映してから、シーケンス指令を処理する
    while (m_sys_stateIn.isNew()) {
        m_sys_stateIn.read();
        m_engine->on_system_state(
            seqmgr::Packet(m_sys_state.data.get_buffer(),
                           m_sys_state.data.get_buffer() + m_sys_state.data.length()));
    }
    while (m_seq_cmdIn.isNew()) {
        m_seq_cmdIn.read();
        m_engine->on_seq_command(
            seqmgr::Packet(m_seq_cmd.data.get_buffer(),
                           m_seq_cmd.data.get_buffer() + m_seq_cmd.data.length()));
    }

    for (const auto& p : m_engine->take_commands()) write_packet(m_cmd, m_cmdOut, p);

    const auto now = std::chrono::steady_clock::now();
    const bool periodic =
        m_state_period_ms > 0 &&
        now - m_last_state_sent >= std::chrono::milliseconds(m_state_period_ms);
    if (periodic) {
        m_engine->take_changed_states();  // 全 Bot 分を送るので、変化分は捨ててよい
        for (const auto& p : m_engine->all_states()) write_packet(m_seq_state, m_seq_stateOut, p);
        m_last_state_sent = now;
    } else {
        for (const auto& p : m_engine->take_changed_states()) write_packet(m_seq_state, m_seq_stateOut, p);
    }

    flush_logs();
    return RTC::RTC_OK;
}

void bot_sequence_manager::write_packet(RTC::TimedLongSeq& data,
                                        RTC::OutPort<RTC::TimedLongSeq>& port,
                                        const seqmgr::Packet& p) {
    data.data.length(static_cast<CORBA::ULong>(p.size()));
    for (CORBA::ULong i = 0; i < p.size(); ++i) data.data[i] = p[i];
    setTimestamp(data);
    port.write();
}

void bot_sequence_manager::flush_logs() {
    for (const auto& s : m_engine->take_logs()) std::cout << "[bot_sequence_manager] " << s << std::endl;
}

extern "C" {
void bot_sequence_managerInit(RTC::Manager* manager) {
    coil::Properties profile(bot_sequence_manager_spec);
    manager->registerFactory(profile, RTC::Create<bot_sequence_manager>,
                             RTC::Delete<bot_sequence_manager>);
}
}
