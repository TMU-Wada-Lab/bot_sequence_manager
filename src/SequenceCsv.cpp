// -*- C++ -*-
/*!
 * @file  SequenceCsv.cpp
 * @brief bot_sequence.csv の読み込み
 */
#include "SequenceCsv.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "bot_sequence_csv.h"

namespace seqmgr {
namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> cols;
    std::string col;
    std::istringstream ss(line);
    while (std::getline(ss, col, ',')) cols.push_back(trim(col));
    if (!line.empty() && line.back() == ',') cols.push_back("");  // 末尾の空欄
    return cols;
}

bool parse_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (*end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

std::string at_line(int line) { return "CSV " + std::to_string(line) + " 行目: "; }

}  // namespace

bool parse_sequence_csv(std::istream& in, SequenceTable& out, std::string& error) {
    using milo::SEQ_CSV_COLUMN;
    using milo::to_int;
    const int col_seq    = to_int(SEQ_CSV_COLUMN::SEQUENCE);
    const int col_step   = to_int(SEQ_CSV_COLUMN::STEP);
    const int col_cmd    = to_int(SEQ_CSV_COLUMN::COMMAND);
    const int col_target = to_int(SEQ_CSV_COLUMN::TARGET);
    const int col_param  = to_int(SEQ_CSV_COLUMN::PARAM);

    out.clear();
    std::string line;
    int line_no = 0;
    bool header_done = false;

    while (std::getline(in, line)) {
        ++line_no;
        if (line_no == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);  // UTF-8 の BOM（Excel で保存すると付く）
        }
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;  // 空行・コメント行

        const auto cols = split_csv_line(t);

        if (!header_done) {
            if (cols.size() < 4 || cols[col_seq] != "sequence" || cols[col_step] != "step" ||
                cols[col_cmd] != "command" || cols[col_target] != "target") {
                error = at_line(line_no) + "見出しは sequence,step,command,target,param にしてください";
                return false;
            }
            header_done = true;
            continue;
        }

        if (cols.size() < 4) {
            error = at_line(line_no) + "列が足りません（sequence,step,command,target が必要）";
            return false;
        }

        milo::SEQUENCE_ID seq_id;
        if (!milo::csv_sequence_from_name(cols[col_seq].c_str(), seq_id)) {
            error = at_line(line_no) + "シーケンス名 \"" + cols[col_seq] + "\" は使えません";
            return false;
        }

        int step = 0;
        if (!parse_int(cols[col_step], step)) {
            error = at_line(line_no) + "step \"" + cols[col_step] + "\" が数値ではありません";
            return false;
        }

        milo::COMMAND cmd;
        if (!milo::csv_command_from_name(cols[col_cmd].c_str(), cmd)) {
            error = at_line(line_no) + "コマンド \"" + cols[col_cmd] +
                    "\" は CSV に書けません（bot_sequence_csv.h の CSV_COMMANDS を参照）";
            return false;
        }

        int target = 0;
        if (!milo::csv_target_from_name(cols[col_target].c_str(), target)) {
            error = at_line(line_no) + "target \"" + cols[col_target] + "\" は使えません";
            return false;
        }

        int param = 0;
        if (static_cast<int>(cols.size()) > col_param && !cols[col_param].empty() &&
            !parse_int(cols[col_param], param)) {
            error = at_line(line_no) + "param \"" + cols[col_param] + "\" が数値ではありません";
            return false;
        }

        auto& steps = out[milo::to_int(seq_id)];
        const int expected = static_cast<int>(steps.size()) + 1;
        if (step != expected) {
            error = at_line(line_no) + cols[col_seq] + " の step は " + std::to_string(expected) +
                    " のはずが " + std::to_string(step) + " になっています（1 から順に書く）";
            return false;
        }
        steps.push_back(SequenceStep{line_no, cmd, target, param});
    }

    if (!header_done) {
        error = "CSV が空です";
        return false;
    }
    return true;
}

bool load_sequence_csv(const std::string& path, SequenceTable& out, std::string& error) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        error = "CSV ファイルを開けません: " + path;
        return false;
    }
    return parse_sequence_csv(ifs, out, error);
}

}  // namespace seqmgr
