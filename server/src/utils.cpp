#include "utils.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <string_view>
#include <unordered_set>

#include "spdlog/spdlog.h"

namespace metalware::utils {

bool is_path_part_of_path(const fs::path& path, const fs::path& parent_path) {
  auto it1 = path.begin();
  auto it2 = parent_path.begin();

  while (it1 != path.end() && it2 != parent_path.end()) {
    if (*it1 != *it2) {
      return false;
    }
    ++it1;
    ++it2;
  }

  return it2 == parent_path.end();
}

bool is_path_excluded(const fs::path& path, const std::vector<fs::path>& exclude_paths) {
  for (const auto& exclude_path : exclude_paths) {
    if (is_path_part_of_path(path, /* parent*/ exclude_path)) {
      return true;
    }
  }

  return false;
}

#if defined(_WIN32)
// Function to decode percent-encoded characters
std::string percent_decode(const std::string& input) {
  std::string result;
  for (size_t i = 0; i < input.length(); ++i) {
    if (input[i] == '%' && i + 2 < input.length()) {
      std::string hex = input.substr(i + 1, 2);
      char decoded_char = static_cast<char>(std::stoi(hex, nullptr, 16));
      result += decoded_char;
      i += 2;  // Skip next two characters
    } else {
      result += input[i];
    }
  }
  return result;
}

// Function to percent-encode characters
std::string percent_encode(const std::string& input) {
  std::ostringstream encoded;
  for (unsigned char c : input) {
    // Encode special characters
    if (!isalnum(c) && c != '-' && c != '_' && c != '.' && c != '~' && c != '/') {
      encoded << '%' << std::uppercase << std::setw(2) << std::setfill('0') << std::hex
              << static_cast<int>(c);
    } else {
      encoded << c;
    }
  }
  return encoded.str();
}
#endif

std::string path_to_uri(const fs::path& p) {
  std::string path_str = p.string();
#if defined(_WIN32)
  std::replace(path_str.begin(), path_str.end(), '\\', '/');
  path_str = percent_encode(path_str);
  return "file:///" + path_str;
#endif
  return "file://" + path_str;
}

// Windows-only: normalize drive letter in path. VScode uses a lower-case letters for drives,
// whereas the backend (slang) upper-case.
// TODO: unit tests
void normalize_path(std::string& p) {
#if defined(_WIN32)
  if (p.size() > 1)
    p[0] = std::toupper(p[0]);
#endif
}

fs::path uri_to_path(const std::string& uri) {
  std::string path;
#if defined(_WIN32)
  if (uri.size() > 9 && uri.substr(0, 8) == "file:///") {
    path = uri.substr(8);
    path = percent_decode(path);
    std::replace(path.begin(), path.end(), '/', '\\');
    normalize_path(path);
  } else {
    spdlog::error("Invalid URI: {}", uri);
  }
#else
  path = (uri.size() > 8 && uri.substr(0, 7) == "file://") ? uri.substr(7) : "";
#endif
  return {path};
}

}  // namespace metalware::utils
