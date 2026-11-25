// -*- C++ -*-
/*!
 * @file  fake_task_provider.cpp
 * @brief テスト用の偽 Task_provider RTC（単体起動用の main も含む）
 *
 * ポート（すべて RTC::TimedLongSeq）
 *   InPort  cmd       ：コマンド（CMD_PACKET）。CSV管理の cmd（または GUI）とつなぐ
 *   OutPort sys_state ：システム状態（SYS_STATE_PACKET）。Bot ごとに 1 配列を周期送信
 */
#include <rtm/idl/BasicDataTypeSkel.h>
#include <rtm/Manager.h>
#include <rtm/DataFlowComponentBase.h>
#include <rtm/DataInPort.h>
#include <rtm/DataOutPort.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "FakeTaskProvider.h"

// clang-format off
static const char* const fake_task_provider_spec[] =
  {
    "implementation_id", "fake_task_provider",
    "type_name",         "fake_task_provider",
    "description",       "Fake Task_provider for testing",
    "version",           "1.0.0",
    "vendor",            "TMU-Wada-Lab",
    "category",          "MiLoBotTest",
    "activity_type",     "PERIODIC",
    "kind",              "DataFlowComponent",
    "max_instance",      "1",
    "language",          "C++",
    "lang_type",         "compile",
    "conf.default.bot_ids",         "1",
    "conf.default.initial_state",   "1",
    "conf.default.step_time_ms",    "2000",
    "conf.default.state_period_ms", "200",
    "conf.default.fail_command",    "-1",
    "conf.default.fail_mode",       "1",
    "conf.default.fail_code",       "1",
    "conf.default.fail_resumable",  "1",
    ""
  };
// clang-format on

class fake_task_provider : public RTC::DataFlowComponentBase {
public:
    explicit fake_task_provider(RTC::Manager* m)
        : RTC::DataFlowComponentBase(m), m_cmdIn("cmd", m_cmd), m_stateOut("sys_state", m_state) {}

    RTC::ReturnCode_t onInitialize() override {
        addInPort("cmd", m_cmdIn);
        addOutPort("sys_state", m_stateOut);
        bindParameter("bot_ids", m_bot_ids, "1");
        bindParameter("initial_state", m_initial_state, "1");
        bindParameter("step_time_ms", m_step_time_ms, "2000");
        bindParameter("state_period_ms", m_state_period_ms, "200");
        bindParameter("fail_command", m_fail_command, "-1");
        bindParameter("fail_mode", m_fail_mode, "1");
        bindParameter("fail_code", m_fail_code, "1");
        bindParameter("fail_resumable", m_fail_resumable, "1");
        return RTC::RTC_OK;
    }

    RTC::ReturnCode_t onActivated(RTC::UniqueId) override {
        fake_tp::Settings s;
        s.bot_ids.clear();
        std::stringstream ss(m_bot_ids);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) s.bot_ids.push_back(std::atoi(item.c_str()));
        }
        s.initial_state  = m_initial_state;
        s.step_time_ms   = m_step_time_ms;
        s.fail_command   = m_fail_command;
        s.fail_mode      = m_fail_mode;
        s.fail_code      = m_fail_code;
        s.fail_resumable = m_fail_resumable;
        m_core.reset(new fake_tp::FakeTaskProvider(s));
        m_last_sent = now_ms() - m_state_period_ms;  // すぐに 1 回送る
        std::cout << "[fake_task_provider] Bot " << m_bot_ids << " で開始（完了まで " << m_step_time_ms
                  << " ms）" << std::endl;
        return RTC::RTC_OK;
    }

    RTC::ReturnCode_t onDeactivated(RTC::UniqueId) override {
        m_core.reset();
        return RTC::RTC_OK;
    }

    RTC::ReturnCode_t onExecute(RTC::UniqueId) override {
        if (!m_core) return RTC::RTC_OK;
        const int64_t now = now_ms();
        bool changed = false;
        while (m_cmdIn.isNew()) {
            m_cmdIn.read();
            fake_tp::Packet p(m_cmd.data.get_buffer(), m_cmd.data.get_buffer() + m_cmd.data.length());
            m_core->on_command(p, now);
            changed = true;
        }
        m_core->tick(now);
        for (const auto& s : m_core->take_logs()) {
            std::cout << "[fake_task_provider] " << s << std::endl;
            changed = true;
        }
        if (changed || now - m_last_sent >= m_state_period_ms) {
            for (const auto& p : m_core->states()) {
                m_state.data.length(static_cast<CORBA::ULong>(p.size()));
                for (CORBA::ULong i = 0; i < p.size(); ++i) m_state.data[i] = p[i];
                setTimestamp(m_state);
                m_stateOut.write();
            }
            m_last_sent = now;
        }
        return RTC::RTC_OK;
    }

private:
    static int64_t now_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    std::string m_bot_ids;
    int m_initial_state = 1, m_step_time_ms = 2000, m_state_period_ms = 200;
    int m_fail_command = -1, m_fail_mode = 1, m_fail_code = 1, m_fail_resumable = 1;

    RTC::TimedLongSeq m_cmd;
    RTC::InPort<RTC::TimedLongSeq> m_cmdIn;
    RTC::TimedLongSeq m_state;
    RTC::OutPort<RTC::TimedLongSeq> m_stateOut;

    std::unique_ptr<fake_tp::FakeTaskProvider> m_core;
    int64_t m_last_sent = 0;
};

static void ModuleInit(RTC::Manager* manager) {
    coil::Properties profile(fake_task_provider_spec);
    manager->registerFactory(profile, RTC::Create<fake_task_provider>, RTC::Delete<fake_task_provider>);
    if (manager->createComponent("fake_task_provider") == nullptr) {
        std::cerr << "Component create failed." << std::endl;
        std::abort();
    }
}

int main(int argc, char** argv) {
    RTC::Manager* manager = RTC::Manager::init(argc, argv);
    manager->setModuleInitProc(ModuleInit);
    manager->activateManager();
    manager->runManager();
    return 0;
}
