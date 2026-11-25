// -*- C++ -*-
/*!
 * @file  bot_sequence_manager.h
 * @brief CSV管理 RTC：シーケンス CSV を読み、ステップを 1 つずつ送る
 *
 * ポート（すべて RTC::TimedLongSeq）
 *   InPort  seq_cmd   ：GUI → CSV管理（シーケンス指令、CMD_PACKET 形式）
 *   InPort  sys_state ：GUI → CSV管理（システム状態、SYS_STATE_PACKET 形式）
 *   OutPort cmd       ：CSV管理 → GUI（コマンド、CMD_PACKET 形式）
 *   OutPort seq_state ：CSV管理 → GUI（シーケンス状態、SEQ_STATE_PACKET 形式）
 */
#ifndef BOT_SEQUENCE_MANAGER_H
#define BOT_SEQUENCE_MANAGER_H

#include <rtm/idl/BasicDataTypeSkel.h>
#include <rtm/Manager.h>
#include <rtm/DataFlowComponentBase.h>
#include <rtm/DataInPort.h>
#include <rtm/DataOutPort.h>

#include <chrono>
#include <memory>
#include <string>

#include "SequenceEngine.h"

class bot_sequence_manager : public RTC::DataFlowComponentBase {
public:
    explicit bot_sequence_manager(RTC::Manager* manager);
    ~bot_sequence_manager() override;

    RTC::ReturnCode_t onInitialize() override;
    RTC::ReturnCode_t onActivated(RTC::UniqueId ec_id) override;
    RTC::ReturnCode_t onDeactivated(RTC::UniqueId ec_id) override;
    RTC::ReturnCode_t onExecute(RTC::UniqueId ec_id) override;

private:
    void write_packet(RTC::TimedLongSeq& data, RTC::OutPort<RTC::TimedLongSeq>& port,
                      const seqmgr::Packet& p);
    void flush_logs();

    // コンフィギュレーション
    std::string m_csv_file;         // 読み込む CSV のパス
    int         m_state_period_ms;  // シーケンス状態を全 Bot 分送る周期（0 で変化時のみ）

    // ポート
    RTC::TimedLongSeq m_seq_cmd;
    RTC::InPort<RTC::TimedLongSeq> m_seq_cmdIn;
    RTC::TimedLongSeq m_sys_state;
    RTC::InPort<RTC::TimedLongSeq> m_sys_stateIn;
    RTC::TimedLongSeq m_cmd;
    RTC::OutPort<RTC::TimedLongSeq> m_cmdOut;
    RTC::TimedLongSeq m_seq_state;
    RTC::OutPort<RTC::TimedLongSeq> m_seq_stateOut;

    std::unique_ptr<seqmgr::SequenceEngine> m_engine;
    std::chrono::steady_clock::time_point m_last_state_sent;
};

extern "C" {
DLL_EXPORT void bot_sequence_managerInit(RTC::Manager* manager);
}

#endif  // BOT_SEQUENCE_MANAGER_H
