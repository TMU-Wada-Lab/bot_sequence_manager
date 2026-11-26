// -*- C++ -*-
/*!
 * @file  launch_dir.h
 * @brief exe をダブルクリックで起動しても、コマンドで起動したときと同じ設定を読むための準備
 *
 * rtc.conf・bot_sequence_manager.conf・CSV の場所は「起動したフォルダ」から見た相対パスで書いてある。
 * ダブルクリックだと起動したフォルダが build/Debug などになり、どれも見つからない。
 * そこで -f が指定されていないときは、
 *   1. 起動したフォルダに rtc.conf があれば、そのまま使う
 *   2. なければ exe のあるフォルダから上へ rtc.conf を探し、見つかったフォルダへ移る
 * としてから、-f rtc.conf を付けて RTC::Manager::init に渡す。
 */
#ifndef BOT_SEQUENCE_MANAGER_LAUNCH_DIR_H
#define BOT_SEQUENCE_MANAGER_LAUNCH_DIR_H

#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace launch_dir {

#ifdef _WIN32
using native_string = std::wstring;
inline native_string exe_dir() {
    std::vector<wchar_t> buf(32768);
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    native_string path(buf.data(), n);
    size_t pos = path.find_last_of(L"\\/");
    return pos == native_string::npos ? native_string() : path.substr(0, pos);
}
inline bool file_exists(const native_string& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
inline native_string join(const native_string& dir, const char* name) {
    native_string n(name, name + std::char_traits<char>::length(name));
    return dir.empty() ? n : dir + L"\\" + n;
}
inline native_string parent(const native_string& dir) {
    size_t pos = dir.find_last_of(L"\\/");
    if (pos == native_string::npos || pos < 3) return native_string();  // C:\ より上はない
    return dir.substr(0, pos);
}
inline bool change_dir(const native_string& dir) { return SetCurrentDirectoryW(dir.c_str()) != 0; }
inline std::string to_utf8(const native_string& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr);
    return out;
}
#else
using native_string = std::string;
inline native_string exe_dir() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return native_string();
    native_string path(buf, static_cast<size_t>(n));
    size_t pos = path.find_last_of('/');
    return pos == native_string::npos ? native_string() : path.substr(0, pos);
}
inline bool file_exists(const native_string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
inline native_string join(const native_string& dir, const char* name) { return dir.empty() ? name : dir + "/" + name; }
inline native_string parent(const native_string& dir) {
    size_t pos = dir.find_last_of('/');
    if (pos == native_string::npos || pos == 0) return native_string();
    return dir.substr(0, pos);
}
inline bool change_dir(const native_string& dir) { return chdir(dir.c_str()) == 0; }
inline std::string to_utf8(const native_string& s) { return s; }
#endif

/*!
 * @brief 起動フォルダを整え、RTC::Manager::init に渡す引数を返す
 * @param tag  メッセージの先頭に付ける名前
 * @param args 戻り値の char* が指す文字列の置き場所（main が終わるまで生かしておく）
 */
inline std::vector<char*> prepare(int argc, char** argv, const char* tag, std::vector<std::string>& args) {
#ifdef _WIN32
    // 文字列は UTF-8（/utf-8 でビルド）。ダブルクリックで開くコンソールは Shift_JIS（932）なので、
    // そのままだと日本語が化ける。このプロセスのコンソールを UTF-8 表示にする。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    args.assign(argv, argv + argc);
    bool has_conf = false;
    for (int i = 1; i < argc; ++i) {
        if (args[i] == "-f") has_conf = true;
    }
    if (!has_conf) {
        if (!file_exists(join(native_string(), "rtc.conf"))) {
            native_string found;
            for (native_string dir = exe_dir(); !dir.empty(); dir = parent(dir)) {
                if (file_exists(join(dir, "rtc.conf"))) { found = dir; break; }
            }
            if (found.empty()) {
                std::cerr << "[" << tag << "] rtc.conf が見つからないので、OpenRTM の既定の設定で起動します" << std::endl;
            } else if (change_dir(found)) {
                std::cout << "[" << tag << "] 起動フォルダ: " << to_utf8(found) << std::endl;
            } else {
                std::cerr << "[" << tag << "] フォルダを移れません: " << to_utf8(found) << std::endl;
            }
        }
        if (file_exists(join(native_string(), "rtc.conf"))) {
            args.push_back("-f");
            args.push_back("rtc.conf");
        }
    }
    std::vector<char*> out;
    for (auto& a : args) out.push_back(&a[0]);
    out.push_back(nullptr);
    return out;
}

}  // namespace launch_dir

#endif  // BOT_SEQUENCE_MANAGER_LAUNCH_DIR_H
