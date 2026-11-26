// -*- C++ -*-
/*!
 * @file  bot_sequence_managerComp.cpp
 * @brief CSV管理 RTC の単体起動用
 */
#include <rtm/Manager.h>

#include <cstdlib>
#include <iostream>

#include "bot_sequence_manager.h"
#include "launch_dir.h"  // windows.h を含むので最後に置く

void MyModuleInit(RTC::Manager* manager) {
    bot_sequence_managerInit(manager);
    RTC::RtcBase* comp = manager->createComponent("bot_sequence_manager");
    if (comp == nullptr) {
        std::cerr << "Component create failed." << std::endl;
        std::abort();
    }
}

int main(int argc, char** argv) {
    // ダブルクリックで起動しても rtc.conf・CSV を読めるよう、起動フォルダを整える
    std::vector<std::string> args;
    std::vector<char*> av = launch_dir::prepare(argc, argv, "bot_sequence_manager", args);
    RTC::Manager* manager = RTC::Manager::init(static_cast<int>(av.size()) - 1, av.data());
    manager->setModuleInitProc(MyModuleInit);
    manager->activateManager();
    manager->runManager();
    return 0;
}
