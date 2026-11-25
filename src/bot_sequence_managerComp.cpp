// -*- C++ -*-
/*!
 * @file  bot_sequence_managerComp.cpp
 * @brief CSV管理 RTC の単体起動用
 */
#include <rtm/Manager.h>

#include <cstdlib>
#include <iostream>

#include "bot_sequence_manager.h"

void MyModuleInit(RTC::Manager* manager) {
    bot_sequence_managerInit(manager);
    RTC::RtcBase* comp = manager->createComponent("bot_sequence_manager");
    if (comp == nullptr) {
        std::cerr << "Component create failed." << std::endl;
        std::abort();
    }
}

int main(int argc, char** argv) {
    RTC::Manager* manager = RTC::Manager::init(argc, argv);
    manager->setModuleInitProc(MyModuleInit);
    manager->activateManager();
    manager->runManager();
    return 0;
}
