#pragma once
#include <filesystem>
#include <set>
#include <vector>
#include <map>
#include <algorithm>

namespace fs = std::filesystem;
namespace metalware::utils {

bool is_path_excluded(const fs::path& path, const std::vector<fs::path>& exclude_paths);
bool is_path_part_of_path(const fs::path& path, const fs::path& parent_path);

// STRING UTILS
inline std::string& ltrim(std::string& s, const char* t = " \t\n\r\f\v") {
  s.erase(0, s.find_first_not_of(t));
  return s;
}

inline std::string& rtrim(std::string& s, const char* t = " \t\n\r\f\v") {
  s.erase(s.find_last_not_of(t) + 1);
  return s;
}

inline void inplace_ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

inline void inplace_rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

inline void trim(std::string &s) {
    inplace_ltrim(s);
    inplace_rtrim(s);
}

fs::path uri_to_path(const std::string& uri);
std::string path_to_uri(const fs::path& p);
void normalize_path(std::string& p);
#if defined(_WIN32)
std::string percent_decode(const std::string& input);
std::string percent_encode(const std::string& input);
#endif
}
