#include "rootunit.hpp"

#include <array>
#include <fstream>
#include <regex>
#include <unordered_set>

#include "spdlog/spdlog.h"
#include "utils.hpp"

using namespace metalware::utils;

namespace {
// TODO: fix potential filename -> filepath bijection failing
// Keep these in sync!
static constexpr size_t SUPPORTED_SOURCE_EXTS_SIZE = 5;
static constexpr std::array<std::string_view, SUPPORTED_SOURCE_EXTS_SIZE> supported_source_exts = {
    ".sv", ".v", ".SV", ".V", ".verilog"};
static constexpr size_t SUPPORTED_HEADER_EXTS_SIZE = 6;
static constexpr std::array<std::string_view, SUPPORTED_HEADER_EXTS_SIZE> supported_header_exts = {
    ".svh", ".vh", ".SVH", ".VH", ".verilogh", ".h"};

static constexpr size_t SCAN_MAX_FILES = 1e6;
static constexpr size_t HDL_MAX_FILES = 1e4;

static constexpr std::string_view REGEX_ALL_INCLUDE_PATTERN =
    "^\\s*`include\\s+\"([^\"]+\\.(?:sv|v|SV|V|verilog|svh|vh|SVH|VH|verilogh|h))\"\\s*$";

static constexpr std::string_view REGEX_NON_HEADER_INCLUDE_PATTERN =
    "^\\s*`include\\s+\"([^\"]+\\.(?:sv|v|SV|V|verilog))\"\\s*$";

static_assert(supported_source_exts.size() == SUPPORTED_SOURCE_EXTS_SIZE);
static_assert(supported_header_exts.size() == SUPPORTED_HEADER_EXTS_SIZE);

static const std::regex all_include_regex(REGEX_ALL_INCLUDE_PATTERN.data());
static const std::regex non_header_include_regex(REGEX_NON_HEADER_INCLUDE_PATTERN.data());

struct PathHash {
  std::size_t operator()(const fs::path& path) const {
    return std::hash<std::string>{}(path.string());
  }
};

bool is_supported_source_ext(const std::string& ext) {
  return std::find(supported_source_exts.begin(), supported_source_exts.end(), ext) !=
         supported_source_exts.end();
}

bool is_supported_header_ext(const std::string& ext) {
  return std::find(supported_header_exts.begin(), supported_header_exts.end(), ext) !=
         supported_header_exts.end();
}

void find_inlined_file(const std::string& line,
    const std::map<std::string, std::set<std::filesystem::path>>& name_to_paths,
    std::set<fs::path>& included_files,
    size_t& files_not_found_in_map) {
  std::smatch match;
  if (std::regex_search(line, match, all_include_regex) && match.size() > 1) {
    const auto& include_paths = name_to_paths.find(match[1].str());
    if (include_paths != name_to_paths.end()) {
      for (const auto& include_path : include_paths->second) {
        included_files.insert(include_path);
      }
    } else {
      files_not_found_in_map++;
    }
  }
}

/*
 * Fair assumption to make (for now):
 * - Once indexed, the only way to add/remove/update file contents is via LSP API.
 * We only want to scan for files once, but we may want to figure out which files are inlined
 * by other files when any file is changed.
 * - We want to have to hard limits. One for the maximum number of files to index (very large
 * value), and one for the maximum number of SystemVerilog files to cache for compilation (smaller
 * value).
 *
 * - API gets called when file is created: didOpen, didChange
 * - API gets called when file is modified: didChange
 * - API gets called when file is deleted: didClose
 */

void find_inlined_files(const std::set<fs::path>& source_files,
    const std::map<std::string, std::set<std::filesystem::path>>& name_to_paths,
    std::set<fs::path>& included_files) {
  size_t files_not_found_in_map = 0;
  for (const auto& file_path : source_files) {
    std::ifstream file(file_path);
    std::string line;

    while (std::getline(file, line)) {
      find_inlined_file(line, name_to_paths, included_files, files_not_found_in_map);
    }
  }

  if (files_not_found_in_map > 0) {
    spdlog::warn("Found {} files not in the map", files_not_found_in_map);
  }
}

// Finds all source and header files in the given path, excluding any files in an excluded path
// Returns true if the number of files exceeds the maximum file count.
bool find_source_and_header_files(const fs::path& path,
    const std::vector<fs::path>& exclude_paths,
    std::set<fs::path>& source_files,
    std::set<fs::path>& header_files) {
  if (!fs::exists(path))
    return false;

  size_t total_file_count = 0;
  size_t source_file_count = 0;
  size_t skipped_file_count = 0;

  bool exceeded_max_files = false;

  std::unordered_set<fs::path, PathHash> excluded(exclude_paths.begin(), exclude_paths.end());

  auto add_file_based_on_type = [&](const fs::path& path) {
    const std::string ext = path.extension().string();
    if (std::find(supported_source_exts.begin(), supported_source_exts.end(), ext) !=
        supported_source_exts.end()) {
      source_files.insert(path);
      return true;
    } else if (std::find(supported_header_exts.begin(), supported_header_exts.end(), ext) !=
               supported_header_exts.end()) {
      header_files.insert(path);
      return true;
    }
    return false;
  };

  // Check if path is regular file
  if (fs::is_regular_file(path)) {
    add_file_based_on_type(path);
    return exceeded_max_files;
  } else if (!fs::is_directory(path)) {
    spdlog::warn("Path {} is not a regular file or directory", path.string());
    return exceeded_max_files;
  }

  try {
    // We know this is a directory.
    for (auto it = fs::recursive_directory_iterator(path); it != fs::recursive_directory_iterator();
         ++it) {
      const auto& entry = *it;

      const bool skip = is_path_excluded(entry.path(), exclude_paths);

      if (skip) {
        skipped_file_count++;
        it.disable_recursion_pending();  // Skip this directory and all its subdirectories
        continue;
      }

      if (entry.is_directory())
        continue;

      total_file_count++;
      if (total_file_count > SCAN_MAX_FILES) {
        spdlog::warn("Exceeded total file count limit of {}", SCAN_MAX_FILES);
        exceeded_max_files = true;
        break;
      }

      if (source_file_count > HDL_MAX_FILES) {
        spdlog::warn("Exceeded HDL file count limit of {}", HDL_MAX_FILES);
        exceeded_max_files = true;
        break;
      }

      if (add_file_based_on_type(entry.path())) {
        source_file_count++;
      } else {
        skipped_file_count++;
      }
    }
  } catch (const fs::filesystem_error& e) {
    spdlog::error("Error while traversing directory: {}", e.what());
  }

  spdlog::info("Found {} hdl files {} total files, skipped {} files",
      source_file_count,
      total_file_count,
      skipped_file_count);
  return exceeded_max_files;
}

std::tuple</*non-inlined source files*/ std::vector<fs::path>,
    /*inlined files*/ std::set<fs::path>,
    /*include name to paths (source files only) */ std::map<std::string, std::set<fs::path>>,
    /*exceeded max file count*/ bool>
find_files(const fs::path& path,
    const std::vector<fs::path>& exclude_paths,
    std::set<fs::path>& sv_files,
    std::set<fs::path>& svh_files) {
  // A non-inlined source file is a file not `include(d) by any other source or header file.
  // For example,UVM lib is a package with a series of definitions included via `include,
  // but it provide no top definition. This function is useful in identifying what sources to push
  // to the preprocessor to avoid duplicate definitions. if modula a in (b.sv) is included
  // in module a (a.sv), we'd only want to pass a.sv to preprocessor as b.sv would be inlined.

  // Step 1. Find source and header files.
  bool exceeded_max_file_count =
      sv_files.empty() && svh_files.empty()
          ? find_source_and_header_files(path, exclude_paths, sv_files, svh_files)
          : false;

  // Step 2. Cache the possible include names of the inlined source files.
  std::map<std::string, std::set<fs::path>> include_name_to_paths;

  auto add_file_possible_include_paths = [&](const fs::path& file) {
    // Add every form of the path from absolute to filename.
    // /this/is/a/path/file.sv -> [file.sv, path/file.sv, a/path/file.sv, is/a/path/file.sv,
    // this/is/a/path/file.sv]
    fs::path p = file;
    std::string suff;
    while (!p.empty()) {
      include_name_to_paths[p.filename().string() + suff].insert(file);

      if (include_name_to_paths.at(p.filename().string() + suff).size() > 1) {
        spdlog::debug("Duplicate include name found: {}", p.filename().string());
      }

      suff = fmt::format("/{}{}", p.filename().string(), suff);

      if (p == p.parent_path()) {  // If root break
        break;
      }
      p = p.parent_path();
    }
  };

  for (const auto& source_file : sv_files) {
    add_file_possible_include_paths(source_file);
  }

  std::set<fs::path> inlined_files;
  for (const auto& file : svh_files)
    inlined_files.insert(file);  // All header files are inlined.

  // Step 3. Find which source files are included by other source files.
  find_inlined_files(sv_files, include_name_to_paths, inlined_files);

  // Step 4. Identify the non-included source files.
  std::vector<fs::path> non_inlined_files;
  for (const auto& file : sv_files) {
    if (inlined_files.find(file) == inlined_files.end()) {
      non_inlined_files.push_back(file);
    }
  }

  return {non_inlined_files, inlined_files, include_name_to_paths, exceeded_max_file_count};
}
}  // namespace

namespace metalware {

class RootUnit::impl {
 public:
  impl(const fs::path& path, bool principal) : path(path), principal(principal) {}

  void store_file_contents(const fs::path& filepath, const std::string& contents) {
    file_buffers[filepath] = contents;
  }

  void clear_file_contents(const fs::path& filepath) {
    auto itr = file_buffers.find(filepath);
    if (itr != file_buffers.end()) {
      file_buffers.erase(itr);
    }
  }

  std::string get_file_contents(const fs::path& filepath) {
    auto itr = file_buffers.find(filepath);
    if (itr != file_buffers.end()) {
      return itr->second;
    }
    return std::string();
  }

  bool add_inlined_file(const std::string& text, const std::vector<fs::path>& excluded_paths) {
    bool ret = false;
    std::set<fs::path> inlined_paths;
    size_t files_not_found_in_map;
    std::istringstream iss(text + "\n");
    std::string line;
    while (std::getline(iss, line)) {
      find_inlined_file(line, include_name_to_paths, inlined_paths, files_not_found_in_map);
    };

    for (const auto& path : inlined_paths) {
      if (!is_path_excluded(path, excluded_paths)) {
        if (std::find(inlined_files.begin(), inlined_files.end(), path) == inlined_files.end()) {
          inlined_files.push_back(path);
        }
        std::erase_if(non_inlined_files, [&](const fs::path& p) {
          return p == path;
        });
        ret = true;
      }
    }

    return ret;
  }

  bool contains_non_header_include(const std::string& text) {
    std::istringstream iss(text + "\n");
    std::string line;
    while (std::getline(iss, line)) {
      std::smatch match;
      if (std::regex_search(line, match, non_header_include_regex) && match.size() > 1) {
        return true;
      }
    }
    return false;
  }

  void get_inlined_files(const std::string& line, std::set<std::string>& inlined_files) {
    auto words_begin = std::sregex_iterator(line.begin(), line.end(), non_header_include_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
      std::smatch match = *i;
      std::string s(match.str());
      trim(s);

      inlined_files.insert(s);
    }
  }

  bool add_file_to_cache(const fs::path& file) {
    // Return false if file is already in cache.
    if (cache.source_files.find(file) != cache.source_files.end() ||
        cache.header_files.find(file) != cache.header_files.end()) {
      spdlog::info("File already in cache: {}", file.string());
      return false;
    }

    if (is_supported_source_ext(file.extension().string())) {
      cache.source_files.insert(file);
      return true;
    } else if (is_supported_header_ext(file.extension().string())) {
      cache.header_files.insert(file);
      return true;
    }

    return false;
  }

  bool remove_file_from_cache(const fs::path& file) {
    if (cache.source_files.erase(file) || cache.header_files.erase(file)) {
      return true;
    }

    return false;
  }

  void clear_paths_cache() {
    cache.source_files.clear();
    cache.header_files.clear();
  }

  ScanResult scan_files(const std::vector<fs::path>& excluded_paths) {
    non_inlined_files.clear();
    inlined_files.clear();
    include_name_to_paths.clear();

    const auto [non_inlined_paths, inlined_paths, include_name_to_paths_map, exceeded_max_files] =
        find_files(path, excluded_paths, cache.source_files, cache.header_files);

    spdlog::info("Found {} non-inlined files (path: {})", non_inlined_paths.size(), path.string());
    for (const auto& path : non_inlined_paths)
      if (!is_path_excluded(path, excluded_paths)) {
        non_inlined_files.push_back(path);
      }

    for (const auto& path : inlined_paths)
      if (!is_path_excluded(path, excluded_paths))
        inlined_files.push_back(path);

    for (const auto& [name, paths] : include_name_to_paths_map)
      for (const auto& path : paths)
        include_name_to_paths[name].insert(path);

    return (exceeded_max_files ? ScanResult::ExceedsMaxFiles : ScanResult::Success);
  }

  const fs::path& path_() const {
    return path;
  }

  const std::unordered_map<fs::path, std::string>& file_buffers_() const {
    return file_buffers;
  }

  const std::vector<fs::path>& non_inlined_files_() const {
    return non_inlined_files;
  }

  const std::vector<fs::path>& inlined_files_() const {
    return inlined_files;
  }

  const std::map<std::string, std::set<fs::path>>& include_name_to_paths_() const {
    return include_name_to_paths;
  }

  const std::set<fs::path>& header_files_() const {
    return cache.header_files;
  }

  bool stale_() const {
    return stale;
  }

  void set_stale(bool stale) {
    this->stale = stale;
  }

  bool principal_() const {
    return principal;
  }

 private:
  fs::path path = {};

  std::unordered_map<fs::path, std::string> file_buffers = {};
  std::vector<fs::path> non_inlined_files = {};
  std::vector<fs::path> inlined_files = {};
  std::map<std::string, std::set<fs::path>> include_name_to_paths = {};  // non-header files only

  bool stale = true;       // whether this needs a rescan
  bool principal = false;  // whether it contains the dot file
                           //
  SourceFilesCache cache = {};
};

RootUnitPtr RootUnit::create(const fs::path& path, bool principal) {
  return std::make_shared<RootUnit>(path, principal);
}

RootUnit::RootUnit(const fs::path& path, bool principal)
    : p_impl(std::make_unique<impl>(path, principal)) {}

ScanResult RootUnit::scan_files(const std::vector<fs::path>& excluded_paths) {
  return p_impl->scan_files(excluded_paths);
}

std::string RootUnit::get_file_contents(const fs::path& filepath) {
  return p_impl->get_file_contents(filepath);
}

void RootUnit::store_file_contents(const fs::path& filepath, const std::string& contents) {
  p_impl->store_file_contents(filepath, contents);
}

void RootUnit::clear_file_contents(const fs::path& filepath) {
  return p_impl->clear_file_contents(filepath);
}

bool RootUnit::add_inlined_file(
    const std::string& text, const std::vector<fs::path>& excluded_paths) {
  return p_impl->add_inlined_file(text, excluded_paths);
}

bool RootUnit::contains_non_header_include(const std::string& text) {
  return p_impl->contains_non_header_include(text);
}

void RootUnit::get_inlined_files(const std::string& line, std::set<std::string>& inlined_files) {
  return p_impl->get_inlined_files(line, inlined_files);
}

bool RootUnit::add_file_to_cache(const fs::path& file) {
  return p_impl->add_file_to_cache(file);
}

bool RootUnit::remove_file_from_cache(const fs::path& file) {
  return p_impl->remove_file_from_cache(file);
}

void RootUnit::clear_paths_cache() {
  p_impl->clear_paths_cache();
}

const fs::path& RootUnit::path() const {
  return p_impl->path_();
}

const std::set<fs::path>& RootUnit::header_files() const {
  return p_impl->header_files_();
}

const std::unordered_map<fs::path, std::string>& RootUnit::file_buffers() const {
  return p_impl->file_buffers_();
}

const std::vector<fs::path>& RootUnit::non_inlined_files() const {
  return p_impl->non_inlined_files_();
}

const std::vector<fs::path>& RootUnit::inlined_files() const {
  return p_impl->inlined_files_();
}

const std::map<std::string, std::set<fs::path>>& RootUnit::include_name_to_paths() const {
  return p_impl->include_name_to_paths_();
}

bool RootUnit::stale() const {
  return p_impl->stale_();
}

void RootUnit::set_stale(bool stale) {
  p_impl->set_stale(stale);
}

bool RootUnit::principal() const {
  return p_impl->principal_();
}
}  // namespace metalware
