#pragma once

#include <optional>
#include <string>
#include <vector>
#include <filesystem>

#include "nlohmann/json.hpp"
#include "utils.hpp"

namespace metalware {
static constexpr float VERSION = 0.16;

struct ModuleDeclaration {  // module declaration
  std::string name;
  std::vector<std::string> ports;
  std::vector<std::pair<std::string, std::optional<std::string>>> parameters;
};

enum class ConstructType {
  INSTANCE_NAME,
  HIERARCHY_INSTANTIATION,
  MODULE_DECLARATION,
  INCLUDE_DIRECTIVE,
  LIBRARY_INCLUDE_STATEMENT
};

std::string to_string(ConstructType type);

// LSP-specific
struct Position {
  size_t line;
  size_t character;

  [[nodiscard]] nlohmann::json to_json() const {
    return nlohmann::json{{"line", line}, {"character", character}};
  }
};

struct Range {
  Position start;
  Position end;

  [[nodiscard]] nlohmann::json to_json() const {
    return nlohmann::json{{"start", start.to_json()}, {"end", end.to_json()}};
  }
};

struct Location {
  std::filesystem::path uri;
  Range range;

  [[nodiscard]] nlohmann::json to_json() const {
    return nlohmann::json{{"uri", utils::path_to_uri(uri) }, {"range", range.to_json()}};
  }
};

enum class DiagnosticSeverity { Error = 1, Warning = 2, Information = 3, Hint = 4, None = 9999 };

struct Diagnostic {
  std::filesystem::path filepath;
  std::string message;
  DiagnosticSeverity severity = DiagnosticSeverity::Information;
  Range range;
  std::string name;
};

}  // namespace metalware
