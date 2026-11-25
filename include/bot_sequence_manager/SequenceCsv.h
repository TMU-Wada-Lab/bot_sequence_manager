// -*- C++ -*-
/*!
 * @file  SequenceCsv.h
 * @brief bot_sequence.csv を読み込み、シーケンスごとのステップ表にする
 *
 * OpenRTM に依存しない（単体テストできるようにするため）。
 */
#ifndef BOT_SEQUENCE_MANAGER_SEQUENCE_CSV_H
#define BOT_SEQUENCE_MANAGER_SEQUENCE_CSV_H

#include <istream>
#include <map>
#include <string>
#include <vector>

#include "bot_command_def.h"

namespace seqmgr {

// CSV の 1 行（1 ステップ）
struct SequenceStep {
    int           csv_line;  // CSV の行番号（エラー表示用、1 始まり）
    milo::COMMAND command;
    int           target;    // 地点 ID・地図番号など（SELF は 0）
    int           param;     // param 列（空欄は 0）
};

// シーケンス番号（milo::SEQUENCE_ID の値）→ ステップの並び
using SequenceTable = std::map<int, std::vector<SequenceStep>>;

// CSV を読み込む。書き間違いがあれば false を返し、error に理由（行番号つき）を入れる。
bool parse_sequence_csv(std::istream& in, SequenceTable& out, std::string& error);
bool load_sequence_csv(const std::string& path, SequenceTable& out, std::string& error);

}  // namespace seqmgr

#endif  // BOT_SEQUENCE_MANAGER_SEQUENCE_CSV_H
