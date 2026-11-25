cmake_minimum_required(VERSION 3.16)
project(bot_sequence_manager CXX)

# -----------------------------------------------------------------------------
# 設定
#   BOT_SYSTEM_DEF_DIR：bot_command_def.h・bot_sequence_csv.h があるフォルダ
#   BUILD_RTC         ：OFF にすると OpenRTM なしで単体テストだけビルドする
# -----------------------------------------------------------------------------
set(BOT_SYSTEM_DEF_DIR "${PROJECT_SOURCE_DIR}/../bot_system_def" CACHE PATH
    "bot_system_def (bot_command_def.h) directory")
option(BUILD_RTC "Build the OpenRTM component" ON)

if(NOT EXISTS "${BOT_SYSTEM_DEF_DIR}/bot_command_def.h")
  message(FATAL_ERROR "bot_command_def.h が見つかりません: ${BOT_SYSTEM_DEF_DIR}\n"
                      "-DBOT_SYSTEM_DEF_DIR=<bot_system_def のパス> を指定してください")
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8 /W3)   # 日本語コメント・文字列のため /utf-8 が必須
endif()

# ロジック部分（OpenRTM に依存しない）
add_library(seqmgr_core STATIC
  src/SequenceCsv.cpp
  src/SequenceEngine.cpp)
target_include_directories(seqmgr_core PUBLIC
  ${PROJECT_SOURCE_DIR}/include/bot_sequence_manager
  ${BOT_SYSTEM_DEF_DIR})

# 単体テスト
enable_testing()
add_executable(sequence_engine_test test/sequence_engine_test.cpp)
target_link_libraries(sequence_engine_test seqmgr_core)
add_test(NAME sequence_engine_test COMMAND sequence_engine_test)

# RTC 本体
if(BUILD_RTC)
  find_package(OpenRTM REQUIRED)

  foreach(var OPENRTM_INCLUDE_DIRS OPENRTM_LIBRARY_DIRS OPENRTM_LIBRARIES)
    if(DEFINED ${var})
      string(REGEX REPLACE "-[ILl]" ";" ${var} "${${var}}")
      string(REGEX REPLACE " ;" ";" ${var} "${${var}}")
    endif()
  endforeach()

  link_directories(${OPENRTM_LIBRARY_DIRS})

  set(comp_srcs src/bot_sequence_manager.cpp)

  add_library(bot_sequence_manager SHARED ${comp_srcs})
  add_executable(bot_sequence_managerComp src/bot_sequence_managerComp.cpp ${comp_srcs})

  foreach(t bot_sequence_manager bot_sequence_managerComp)
    target_include_directories(${t} PRIVATE ${OPENRTM_INCLUDE_DIRS})
    target_compile_options(${t} PRIVATE ${OPENRTM_CFLAGS})
    target_link_libraries(${t} seqmgr_core ${OPENRTM_LIBRARIES})
  endforeach()
  set_target_properties(bot_sequence_manager PROPERTIES PREFIX "")
endif()
