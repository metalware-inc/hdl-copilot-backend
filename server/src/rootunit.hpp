#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "shared.hpp"

namespace fs = std::filesystem;
namespace metalware {

enum class ScanResult { Success = 1, ExceedsMaxFiles = 2 };

struct SourceFilesCache {
  std::set<fs::path> header_files = {};
  std::set<fs::path> source_files = {};
};

class RootUnit;
using RootUnitPtr = std::shared_ptr<RootUnit>;

// A project is a collection of compilation roots.
// Only root unit is the "principal" unit, which is the one that is contains the dot file.
class RootUnit {
 public:
  static RootUnitPtr create(const fs::path& path, bool principal = false);
  RootUnit(const fs::path& path, bool principal);
  RootUnit(RootUnit const&) = delete;
  RootUnit& operator=(RootUnit const&) = delete;
  RootUnit(RootUnit&&) = delete;
  RootUnit& operator=(RootUnit&&) = delete;

  const fs::path& path() const;
  const std::unordered_map<fs::path, std::string>& file_buffers() const;
  const std::vector<fs::path>& non_inlined_files() const;
  const std::vector<fs::path>& inlined_files() const;
  const std::map<std::string, std::set<fs::path>>& include_name_to_paths() const;
  const std::set<fs::path>& header_files() const;
  bool stale() const;
  bool principal() const;
  void set_stale(bool stale);

  ScanResult scan_files(const std::vector<fs::path>& excluded_paths);

  std::string get_file_contents(const fs::path& filepath);
  void store_file_contents(const fs::path& filepath, const std::string& contents);
  void clear_file_contents(const fs::path& filepath);

  bool add_inlined_file(const std::string& text, const std::vector<fs::path>& excluded_paths);
  bool contains_non_header_include(const std::string& line);
  void get_inlined_files(const std::string& line, std::set<std::string>& inlined_files);
  bool add_file_to_cache(const fs::path& file);
  bool remove_file_from_cache(const fs::path& file);
  void clear_paths_cache();

 private:
  class impl;

  std::unique_ptr<impl> p_impl;
};
}  // namespace metalware
