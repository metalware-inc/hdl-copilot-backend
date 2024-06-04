#include "project.hpp"

#include <fstream>

#include "lookupvisitor.hpp"
#include "packethandler.hpp"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/diagnostics/TextDiagnosticClient.h"
#include "slang/driver/SourceLoader.h"
#include "slang/parsing/Parser.h"
#include "slang/parsing/Preprocessor.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/util/Bag.h"
#include "spdlog/spdlog.h"
#include "utils.hpp"

namespace ss = slang::syntax;

namespace metalware {

namespace {
void add_parent_if_exists(const fs::path &p, std::vector<fs::path> &v) {
  if (p.has_parent_path()) {
    const auto parent = p.parent_path();
    if (std::find(v.begin(), v.end(), parent) == v.end()) {
      v.push_back(parent);
    }
  }
}

void add_include_dirs(const std::set<fs::path> &files, std::vector<fs::path> &includeDirs) {
  // Resolve project-level header files (imperative this comes before source files).
  for (const auto &p : files) {
    if (fs::is_directory(p)) {
      includeDirs.push_back(p);
    }
    add_parent_if_exists(p, includeDirs);
    if (p.has_parent_path())
      add_parent_if_exists(p.parent_path(), includeDirs);
  }
}

bool has_include_statement(const std::string &text) {
  size_t n = text.find("`include", 0);
  if (n != std::string::npos) {
    n = text.find("\"", n + 8);
    if (n != std::string::npos) {
      n = text.find(".", n + 2);
      if (n != std::string::npos) {
        n = text.find("\"", n + 2);
        if (n != std::string::npos) {
          return true;
        }
      }
    }
  }
  return false;
}

}  // namespace

int Project::get_fp_rank(const fs::path &p) {
  // Check if in fp_ranks unoredred_map
  if (fp_ranks.find(p) != fp_ranks.end()) {
    return fp_ranks[p];
  }

  return 9999;
}

void Project::set_fp_rank(const fs::path &path, int rank) {
  fp_ranks[path] = rank;
}

bool Project::add_target_files_to_compilation(const std::vector<fs::path> &target_file_paths,
    const std::shared_ptr<slang::ast::Compilation> &compilation) {
  non_inlined_fp_string_cache.clear();
  for (const auto &p : target_file_paths)
    non_inlined_fp_string_cache.push_back(p.string());

  // Definition: A target file is non-inlined source file passed to the compiler.
  // Create a vector of string_views from the vector of strings
  std::vector<std::string_view> file_paths_views;
  file_paths_views.reserve(target_file_paths.size());  // Reserve to avoid reallocations

  for (const auto &path_str : non_inlined_fp_string_cache) {
    file_paths_views.emplace_back(path_str);
  }

  if (file_paths_views.empty()) {
    spdlog::error("No target files found for compilation");
    return false;
  }

  slang::Bag bag;

  // Add predefines
  slang::parsing::PreprocessorOptions preproc_options;
  preproc_options.predefines.insert(
      preproc_options.predefines.end(), this->defines.begin(), this->defines.end());

  bag.set(preproc_options);
  // Create a span from the vector of string_views
  std::span<std::string_view> paths_span(file_paths_views);

  // Create a single tree (compilation unit) for all top files
  if (const auto tree =
          ss::SyntaxTree::fromFiles(paths_span, *source_manager, bag, source_library.get());
      tree.has_value()) {
    compilation->addSyntaxTree(tree.value());
  } else {
    spdlog::error("Failed to add syntax tree for target files");
    return false;
  }
  return true;
}

// Note: Calling this function assumes scan_files has been called.
// Note: the compilation will cache!
nonstd::expected<std::shared_ptr<slang::ast::Compilation>, std::string> Project::compile() {
  if (cached_compilation.has_value()) {
    spdlog::info("Compilation: using cached compilation!");
    return cached_compilation.value();
  }

  source_manager = std::make_shared<slang::SourceManager>();
  source_library = std::make_shared<slang::SourceLibrary>();
  source_library->isDefault = true;

  slang::Bag bag;
  auto compilation = std::make_shared<slang::ast::Compilation>(bag, source_library.get());

  // Sort root units so that principal root unit is last
  std::vector<std::pair<fs::path, std::shared_ptr<RootUnit>>> sorted_root_units;
  std::vector<fs::path> target_file_paths;
  // Add principal_root_unit last so that definitions contained in other root units
  // are resolved in the principal unit.
  for (const auto &[root_unit_path, root_unit] : root_units) {
    if (root_unit->principal()) {
      continue;
    }
    sorted_root_units.emplace_back(root_unit_path, root_unit);
  }
  sorted_root_units.emplace_back(principal_root_unit->path(), principal_root_unit);

  // Handle each root unit
  for (const auto &[root_unit_path, root_unit] : sorted_root_units) {
    spdlog::info("Handling root unit: {}", root_unit_path.string());
    add_include_dirs(root_unit->header_files(), source_library->includeDirs);

    // Cache open files to source manager.
    for (const auto &[fp, buff] : root_unit->file_buffers()) {
      if (source_manager->isCached(fp.string())) {
        spdlog::warn("Compilation: file already in source manager cache: {}", fp.string());
        continue;
      }

      spdlog::debug("Caching buffered file to source manager: {}", fp.string());
      const auto _ = source_manager->assignText(
          fp.string(), buff, slang::SourceLocation(), source_library.get());
    }

    for (const auto &fp : root_unit->non_inlined_files())
      target_file_paths.push_back(fp);
  }

  // Sort in reverse target_file_paths by their ranks in get_fp_rank(path)
  std::sort(
      target_file_paths.begin(), target_file_paths.end(), [this](const auto &lhs, const auto &rhs) {
        return get_fp_rank(lhs) > get_fp_rank(rhs);
      });

  if (!add_target_files_to_compilation(target_file_paths, compilation)) {
    return nonstd::make_unexpected("Failed to add target files to compilation");
  }

  cached_compilation = compilation;
  return compilation;
}

std::vector<Diagnostic> Project::find_diagnostics() {
  auto last = std::chrono::high_resolution_clock::now();

  cached_compilation = std::nullopt;  // Clear the cached compilation
  auto maybe_compilation = compile();

  if (!maybe_compilation.has_value()) {
    spdlog::error("Compilation failed: {}", maybe_compilation.error());
    return {};
  }

  const auto compilation = maybe_compilation.value();

  spdlog::info("Compilation took: {}ms",
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::high_resolution_clock::now() - last)
          .count());
  last = std::chrono::high_resolution_clock::now();

  const auto sm = compilation->getSourceManager();
  slang::DiagnosticEngine diag_engine(*sm);

  // Add a diagnostic client to capture diagnostics
  const auto client = std::make_shared<slang::TextDiagnosticClient>();
  diag_engine.addClient(client);

  std::vector<Diagnostic> lsp_diagnostics;
  std::set<fs::path> diagnostics_files;

  using DiagCodeLinePath = std::tuple<size_t, size_t, fs::path>;
  using DiagCodePath = std::tuple<size_t, fs::path>;

  std::map<DiagCodeLinePath, slang::Diagnostic> line_suppressed_diagnostics;
  std::map<DiagCodePath, slang::Diagnostic> file_suppressed_diagnostics;

  for (auto &diag : compilation->getLineSuppressedDiagnostics()) {
    const size_t line = sm->getLineNumber(diag.location);
    const size_t column = sm->getColumnNumber(diag.location);
    const auto path = sm->getFullPath(diag.location.buffer());

    line_suppressed_diagnostics[std::make_tuple(line, diag.code.getCode(), path)] = diag;

    spdlog::debug("Line-wide suppressed diagnostic (code: {}) : {}:{}:{}",
        slang::toString(diag.code),
        path.string(),
        line,
        column);
  }

  for (auto &diag : compilation->getFileSuppressedDiagnostics()) {
    const size_t line = sm->getLineNumber(diag.location);
    const size_t column = sm->getColumnNumber(diag.location);
    const auto path = sm->getFullPath(diag.location.buffer());

    file_suppressed_diagnostics[std::make_tuple(diag.code.getCode(), path)] = diag;

    spdlog::debug("File-wide suppressed diagnostic (code: {}) : {}:{}:{}",
        slang::toString(diag.code),
        path.string(),
        line,
        column);
  }

  spdlog::info("Fetching suppressions took: {}ms",
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::high_resolution_clock::now() - last)
          .count());
  last = std::chrono::high_resolution_clock::now();

  size_t empty_path_diagnostics = 0;
  for (auto &diag : compilation->getAllDiagnostics()) {
    // Skip if there is a suppresssed diagnostic on the same line that has the same code
    if (diag.location == slang::SourceLocation::NoLocation) {
      spdlog::warn("No location for diagnostic retrieved from compilation!");
    }
    const size_t line = sm->getLineNumber(diag.location);
    const auto &filepath = sm->getFullPath(diag.location.buffer());

    if (filepath == "") {
      empty_path_diagnostics++;
      continue;
    }

    if (is_resource_excluded(filepath)) {
      continue;
    }

    diag_engine.issue(diag);

    Diagnostic lsp_diag;
    lsp_diag.message = diag_engine.formatMessage(diag);
    lsp_diag.name = slang::toString(diag.code);

    spdlog::debug("Diagnostic is {} fp: {}", lsp_diag.message, filepath.string());

#ifndef IGNORE_ALL_DIAGNOSTIC_FILTERS
    if (line_suppressed_diagnostics.find(std::make_tuple(line, diag.code.getCode(), filepath)) !=
        line_suppressed_diagnostics.end()) {
      continue;
    }

    if (file_suppressed_diagnostics.find(std::make_tuple(diag.code.getCode(), filepath)) !=
        file_suppressed_diagnostics.end()) {
      continue;
    }

    // Search project-wise suppressions only looking at names.
    if (std::find_if(suppressed_diagnostics.begin(),
            suppressed_diagnostics.end(),
            [&diag](const Diagnostic &d) {
              return d.name == slang::toString(diag.code);
            }) != suppressed_diagnostics.end()) {
      continue;
    }
#endif

    size_t column = sm->getColumnNumber(diag.location);

    if (column == 0) {  // Hacky workaround for when the location resolves to an empty path.
      column = 1;
    }

    // Check if    // Convert to full path by combining with the project path
    lsp_diag.filepath = filepath;

#ifndef IGNORE_ALL_DIAGNOSTIC_FILTERS
    // Check if the file is in any of the non-principal root units, in which case we should ignore
    // it linting errors from it.
    bool ignore = false;
    auto unit = get_unit_via_path(lsp_diag.filepath);
    if (unit.has_value() && !unit.value()->principal()) {
      if (utils::is_path_part_of_path(lsp_diag.filepath, unit.value()->path())) {
        ignore = true;
      }
    }

    if (ignore) {
      continue;
    }
#endif

    diagnostics_files.insert(lsp_diag.filepath);

    lsp_diag.range.start.line = line - 1;
    lsp_diag.range.start.character = column - 1;
    lsp_diag.range.end.line = line - 1;
    lsp_diag.range.end.character = column - 1;

    const auto severity = slang::getDefaultSeverity(diag.code);

    switch (severity) {
      case slang::DiagnosticSeverity::Error:
        lsp_diag.severity = DiagnosticSeverity::Error;
        break;
      case slang::DiagnosticSeverity::Warning:
        lsp_diag.severity = DiagnosticSeverity::Warning;
        break;
      case slang::DiagnosticSeverity::Fatal:
        lsp_diag.severity = DiagnosticSeverity::Error;
        break;
      case slang::DiagnosticSeverity::Ignored:
        lsp_diag.severity = DiagnosticSeverity::Hint;
        break;
      case slang::DiagnosticSeverity::Note:
        lsp_diag.severity = DiagnosticSeverity::Information;
        break;
    }

    lsp_diagnostics.push_back(lsp_diag);
  }

  if (empty_path_diagnostics > 0) {
    spdlog::warn("Diagnostics with empty paths skipped: {}", empty_path_diagnostics);
  }

  spdlog::info("Fetching diagnostics took: {}ms",
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::high_resolution_clock::now() - last)
          .count());
  last = std::chrono::high_resolution_clock::now();

  const std::string report = client->getString();
  spdlog::debug(" -> Diagnostics: {}", report);
  spdlog::info(" LSP diagnostics: {}", lsp_diagnostics.size());

  return lsp_diagnostics;
}

// This determines what files are passed to the compiler and caches
// inlined files for lookup.
void Project::scan_files() {
  auto last_all = std::chrono::high_resolution_clock::now();

  for (auto &[path, root_unit] : root_units) {
    if (root_unit->stale())
      root_unit->set_stale(false);
    else {
      spdlog::info("Skipping non-stale root unit: {}", path.string());
      continue;
    }

    auto last = std::chrono::high_resolution_clock::now();

    root_unit->scan_files(excluded_paths);

    spdlog::debug("Unit time to detect scan files (path: {}): {}ms",
        path.string(),
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - last)
            .count());
  }
  spdlog::info("All units time to detect scan files: {}ms",
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::high_resolution_clock::now() - last_all)
          .count());
}

// TODO: make this atomic as there can be concurrent readers.
bool Project::write_dotfile() {
  const auto &principal_root_unit_path = principal_root_unit->path();
  spdlog::info("Writing dotfile for project: {}", principal_root_unit_path.string());

  const auto dot_file_path = principal_root_unit_path / DOT_FILENAME;

  nlohmann::json dotfile;
  std::vector<std::string> non_principal_root_unit_paths;
  for (const auto &[path, root_unit] : root_units) {
    if (!root_unit->principal()) {
      non_principal_root_unit_paths.push_back(path.string());
    }
  }
  dotfile["imports"] = non_principal_root_unit_paths;
  dotfile["projectSuppressions"] = nlohmann::json::array();
  for (const auto &suppression : suppressed_diagnostics) {
    dotfile["projectSuppressions"].push_back(suppression.name);
  }

  std::vector<std::string> paths;
  for (const auto &exclusion : excluded_paths) {
    paths.push_back(exclusion.string());
  }
  dotfile["excludePaths"] = paths;

  dotfile["macros"] = nlohmann::json::array();
  for (const auto &macro : defines) {
    auto pos = macro.find('=');
    if (pos == std::string::npos) {
      dotfile["macros"].push_back({{"name", macro}, {"value", "1"}});
    } else {
      dotfile["macros"].push_back(
          {{"name", macro.substr(0, pos)}, {"value", macro.substr(pos + 1)}});
    }
  }

  if (std::ofstream file(dot_file_path); file.is_open()) {
    file << dotfile.dump(2);
    file.close();
    return true;
  }
  return false;
}

bool Project::load_dotfile(bool scan_files_flag) {
  if (!principal_root_unit) {
    spdlog::error("Base root unit not set");
    return false;
  }

  const auto &principal_root_unit_path = principal_root_unit->path();
  spdlog::debug("Loading dotfile for project: {}", principal_root_unit_path.string());

  const auto dot_file_path = principal_root_unit_path / DOT_FILENAME;

  if (!fs::exists(dot_file_path)) {
    spdlog::error("Dotfile does not exist: {}", dot_file_path.string());
    return false;
  }

  // Read file as json
  std::ifstream file(dot_file_path);
  nlohmann::json dotfile;

  try {
    file >> dotfile;
  } catch (nlohmann::json::parse_error &e) {
    spdlog::warn("Failed to parse dotfile: {}. Overwriting with new!", e.what());
    dotfile = nlohmann::json();
  }

  // Print dotfile for debugging
  spdlog::debug("Dotfile: {}", dotfile.dump(2));

  if (dotfile.contains("macros")) {  // This is an array of {"name": "", "value": ""}.
    defines.clear();
    for (const auto &macro : dotfile["macros"]) {
      if (!macro.contains("name") || !macro.contains("value")) {
        spdlog::warn("Skipping macro as it does not contain name or value");
        continue;
      }

      auto val = macro["value"].get<std::string>();
      if (val.empty()) {
        val = "1";
      }
      defines.push_back(fmt::format("{}={}", macro["name"].get<std::string>(), val));
    }
  }

  if (dotfile.contains("imports")) {
    // Remove all but first root unit
    root_units.clear();
    const auto &principal_root_unit_path = principal_root_unit->path();
    root_units[principal_root_unit_path] = principal_root_unit;

    for (const auto &tmp : dotfile["imports"]) {
      auto path = tmp.get<std::string>();
      utils::normalize_path(path);

      if (!fs::exists(path)) {
        spdlog::warn("Root unit path does not exist: {}", path);
        continue;
      }

      if (path.rfind(principal_root_unit_path.string(), 0) == 0 ||
          principal_root_unit_path.string().rfind(path, 0) == 0) {
        spdlog::warn(
            "Skipping root unit path as it either includes or is included by project dir: {}",
            path);
        continue;
      }

      spdlog::debug("Adding root unit path: {}", path);
      auto root_unit = RootUnit::create(fs::path(path), false);
      root_units[root_unit->path()] = root_unit;
    }
  }

  if (dotfile.contains("projectSuppressions")) {
    suppressed_diagnostics.clear();
    for (const auto &suppression : dotfile["projectSuppressions"]) {
      Diagnostic diag;
      diag.name = suppression.get<std::string>();
      suppressed_diagnostics.push_back(diag);
    }
  }

  if (dotfile.contains("excludePaths")) {
    excluded_paths.clear();
    exclude_rel_paths(dotfile["excludePaths"]);
  }

  if (scan_files_flag)
    scan_files();

  return true;
}

void Project::exclude_rel_paths(const std::vector<std::string> &relative_paths) {
  for (const auto &exclusion : relative_paths) {
    // Create fs::path considering an excluded path is relative to the project path
    fs::path exclusion_path = principal_root_unit->path() / exclusion;
    if (!fs::exists(exclusion_path)) {
      spdlog::warn("Excluded path does not exist: {}", exclusion_path.string());
      continue;
    }
    excluded_paths.emplace_back(exclusion_path.string());
  }

  // Make sure to re-detect top files after excluding paths.
}

std::optional<std::shared_ptr<RootUnit>> Project::get_unit_via_path(const fs::path &path) const {
  for (const auto &[root_unit_path, root_unit] : root_units) {
    if (utils::is_path_part_of_path(path, root_unit_path)) {
      return root_unit;
    }
  }

  return std::nullopt;
}

bool Project::get_text_from_file_loc(
    const fs::path &path, int line_idx, int col, std::string &output_buffer) const {
  auto unit = get_unit_via_path(path);
  if (!unit.has_value()) {
    spdlog::error("Unit not found for path: {}", path.string());
    return false;
  }

  if (unit.value()->file_buffers().find(path) != unit.value()->file_buffers().end()) {
    std::istringstream iss(unit.value()->file_buffers().at(path));
    std::string line;
    for (int i = 0; i <= line_idx; i++) {
      std::getline(iss, line);
    }
    output_buffer = line;
    return true;
  }

  spdlog::error("File buffer not found in project: {}", path.string());
  return false;
}

void Project::update_file_buffer(const fs::path &filepath, const std::string &buff) {
  auto unit = get_unit_via_path(filepath);
  if (!unit.has_value()) {
    spdlog::error("Unit not found for path: {}", filepath.string());
    return;
  }

  bool rescan = false;
  unit.value()->set_stale(true);
  auto prev_contents = unit.value()->get_file_contents(filepath);
  std::set<std::string> added_inlined_files;
  std::set<std::string> deleted_inlined_files;

  if (prev_contents != buff) {
    bool prev_file_read_complete = false;
    bool file_read_complete = false;
    std::istringstream prev_iss(prev_contents);
    std::istringstream iss(buff);
    do {
      // This loop would be more efficient if diff-match-patch returned the line
      // numbers along with the changes, Then we would not have to read
      // the entire file line by line.
      std::string prev_line;
      if (!prev_file_read_complete && !std::getline(prev_iss, prev_line)) {
        prev_file_read_complete = true;
      }

      std::string line;
      if (!file_read_complete && !std::getline(iss, line)) {
        file_read_complete = true;
      }

      if (!prev_file_read_complete && !file_read_complete) {
        if (prev_line != line) {
          // for an insert or delete find all *source* included files in prev_line and line
          // all the included files in prev_line are assumed deleted
          // all the included files in line are assumed added
          // There is a set difference that is performed later to only
          // get the added files and add those to the inlined list. If there
          // exists files only in the deleted set a rescan is performed.
          // The rescan is performed because even though a file may go from
          // inlined -> non-inlined in file foo, it may still be inlined
          // in file bar, so we do not want to prematurely mark it as non-inlined
          // without checking the other files via a rescan.
          spdlog::debug("found prev_line {} line {}", prev_line, line);
          if (has_include_statement(prev_line)) {
            unit.value()->get_inlined_files(prev_line, deleted_inlined_files);
          }
          if (has_include_statement(line)) {
            unit.value()->get_inlined_files(line, added_inlined_files);
          }
        }
      }

      if (prev_file_read_complete && !file_read_complete) {
        if (has_include_statement(line)) {  // This assumes there is only one include per line.
          unit.value()->get_inlined_files(line, added_inlined_files);
        }
      }
      if (!prev_file_read_complete && file_read_complete) {
        if (has_include_statement(prev_line)) {  // This assumes there is only one include per line.
          unit.value()->get_inlined_files(prev_line, deleted_inlined_files);
        }
      }
    } while (!prev_file_read_complete || !file_read_complete);

    std::set<std::string> result;
    std::set_difference(std::begin(added_inlined_files),
        std::end(added_inlined_files),
        std::begin(deleted_inlined_files),
        std::end(deleted_inlined_files),
        std::inserter(result, result.end()));

    if (!result.empty()) {
      for (const auto &file : result) {
        if (unit.value()->add_inlined_file(file, excluded_paths)) {
          spdlog::debug("Added inlined file: {}, rescan = {}", file, rescan);
        } else {
          spdlog::debug("No included files found for line: {} rescan = {}", file, rescan);
        }
      }
    }

    result.clear();
    std::set_difference(std::begin(deleted_inlined_files),
        std::end(deleted_inlined_files),
        std::begin(added_inlined_files),
        std::end(added_inlined_files),
        std::inserter(result, result.end()));

    if (!result.empty()) {
      for (const auto &file : result) {
        rescan = unit.value()->contains_non_header_include(file);
        spdlog::info("Deleted line: {}, rescan = {}", file, rescan);
        if (rescan) {
          break;
        }
      }
    }
  }

  unit.value()->store_file_contents(filepath, buff);
  // We are ok with this failing as there may be no cache.
  if (!unit.value()->add_file_to_cache(filepath)) {
    spdlog::warn("Failed to add file to cache: {}", filepath.string());
  }

  if (rescan) {
    spdlog::info("Rescanning project");
    scan_files();
  }
}

bool Project::add_file(
    const fs::path &path, const std::string &buff) {  // returns false if file already exists
  auto unit = get_unit_via_path(path);
  if (!unit.has_value()) {
    spdlog::error("Unit not found for path: {}", path.string());
    return false;
  }

  unit.value()->set_stale(true);
  if (!buff.empty()) {
    unit.value()->store_file_contents(path, buff);
  }
  return unit.value()->add_file_to_cache(path);
}

void Project::remove_file_if_no_ent(const fs::path &path) {
  auto unit = get_unit_via_path(path);
  if (!unit.has_value()) {
    spdlog::error("Unit not found for path: {}", path.string());
    return;
  }

  unit.value()->set_stale(true);
  unit.value()->clear_file_contents(path);
  if (!unit.value()->remove_file_from_cache(path)) {
    spdlog::error("Failed to remove file from cache: {}", path.string());
  }
}

bool Project::exclude_resource(const fs::path &path) {
  auto unit = get_unit_via_path(path);

  if (!unit.has_value()) {
    spdlog::warn("Unit not found for path: {}", path.string());
    return false;
  }

  unit.value()->clear_paths_cache();

  // Check if path exists
  if (!fs::exists(path)) {
    spdlog::warn("Path does not exist: {}", path.string());
    return false;
  }

  // Check if path is within project path
  if (!utils::is_path_part_of_path(path, unit.value()->path())) {
    spdlog::warn(
        "Path is not within unit path ({}) {}", unit.value()->path().string(), path.string());
    return false;
  }

  if (!load_dotfile(/* scan_files */ false)) {
    spdlog::error("Failed to load dotfile");
    return false;
  }

  // Check if path is excluded
  if (is_resource_excluded(path)) {
    spdlog::warn("Path is already excluded: {}", path.string());
    return true;
  }

  // If path has not been excluded, but there are files within this path that have,
  // remove those paths from the excluded paths list before adding this one.
  std::vector<fs::path> new_excluded_paths;
  for (const auto &p : excluded_paths) {
    if (utils::is_path_part_of_path(p, path /* alleged parent */)) {
      spdlog::debug("Removing path from excluded paths: {}", p.string());
    } else {
      new_excluded_paths.push_back(p);
    }
  }

  new_excluded_paths.push_back(path);
  excluded_paths = new_excluded_paths;

  unit.value()->set_stale(true);
  scan_files();

  return write_dotfile();
}

bool Project::is_resource_excluded(const fs::path &path) {
  return utils::is_path_excluded(path, excluded_paths);
}

bool Project::include_resource(const fs::path &path) {
  auto unit = get_unit_via_path(path);
  if (!unit.has_value()) {
    spdlog::warn("Unit not found for path: {}", path.string());
    return false;
  }

  unit.value()->clear_paths_cache();

  // TODO: handle case where path to include is child of excluded path
  if (!fs::exists(path)) {
    spdlog::warn("Path does not exist: {}", path.string());
    return false;
  }

  // Check if path is within project path
  if (!utils::is_path_part_of_path(path, unit.value()->path())) {
    spdlog::warn(
        "Path is not within unit path ({}) {}", unit.value()->path().string(), path.string());
    return false;
  }

  if (!load_dotfile(/* detect_target_files */ false)) {
    spdlog::error("Failed to load dotfile");
    return false;
  }

  // Remove:
  // - exact path from excluded paths (if present)
  // - children of the path from excluded paths
  std::vector<fs::path> new_excluded_paths;
  for (const auto &p : excluded_paths) {
    if (p != path && !utils::is_path_part_of_path(p, /* parent */ path)) {
      new_excluded_paths.push_back(p);
    }
  }

  // Check if the path to include is within an excluded path,
  // in which case the parent path should be removed from the excluded paths list.
  std::optional<fs::path> ancestor_path;
  for (const auto &p : excluded_paths) {
    if (utils::is_path_part_of_path(path, p)) {
      ancestor_path = p;
      break;
    }
  }

  // Remove excluded ancestor path from new excluded paths if present
  if (ancestor_path.has_value()) {
    if (std::find(new_excluded_paths.begin(), new_excluded_paths.end(), ancestor_path.value()) !=
        new_excluded_paths.end()) {
      new_excluded_paths.erase(
          std::remove(new_excluded_paths.begin(), new_excluded_paths.end(), ancestor_path.value()),
          new_excluded_paths.end());
    }

    // Exclude every child of the ancestor path. If a child contains the target path, it will be
    // will be skipped, and its children will be recursively excluded.
    //
    // function exclude_helper(target)
    //  For each child of target
    //    if child includes path
    //      exclude_helper(child)
    //    else
    //      exclude child
    std::function<void(const fs::path &)> exclude_helper =
        [&new_excluded_paths, &exclude_helper, &path](const fs::path &target) {
          if (fs::is_regular_file(target)) {
            return;
          }

          for (const auto &entry : fs::directory_iterator(target)) {
            if (utils::is_path_part_of_path(path, /*parent path*/ entry.path())) {
              if (path != entry.path()) {
                exclude_helper(entry.path());
              } else {
                // This is the path we wanted to exclude (nop).
              }
            } else {
              // Make sure this is not contained by path to include
              if (!utils::is_path_part_of_path(entry.path(), /* path to include*/ path)) {
                new_excluded_paths.push_back(entry.path());
              }
            }
          }
        };

    exclude_helper(ancestor_path.value());
  }

  excluded_paths = new_excluded_paths;
  unit.value()->set_stale(true);
  scan_files();

  return write_dotfile();
}

bool Project::set_macros(const std::vector<std::pair<std::string, std::string>> &macros) {
  if (!load_dotfile(/* scan_files */ false)) {
    spdlog::error("Failed to load dotfile");
    return false;
  }

  defines.clear();
  for (const auto &[name, value] : macros) {
    if (value.empty()) {
      defines.push_back(name);
    } else {
      defines.push_back(fmt::format("{}={}", name, value));
    }
  }

  return write_dotfile();
}

nonstd::expected<std::shared_ptr<Project>, std::string_view> Project::create(const fs::path &path) {
  std::shared_ptr<Project> project = std::shared_ptr<Project>(new Project(path));

  if (!fs::exists(path)) {
    return nonstd::make_unexpected("Path does not exist"sv);
  }

  spdlog::info("Creating project for path: {}", path.string());

  if (!project->load_dotfile()) {
    return nonstd::make_unexpected("Failed to load dotfile"sv);
  }

  return project;
}

void Project::print_root_unit_paths() {
  for (const auto &[path, root_unit] : root_units) {
    spdlog::info("  - Root unit path: {}", path.string());
  }
}

// Hacky way to get the default (right-hand-side) value in an assignment.
std::optional<std::string> Project::extract_assigned_value(slang::SourceRange range) {
  if (!source_manager) {
    spdlog::error("Source manager not available");
    return std::nullopt;
  }

  // TODO: Is there any way the source code could change while we are extracting the value?
  if (range.start().buffer() == range.end().buffer()) {
    const auto txt = source_manager->getSourceText(range.start().buffer());

    if (txt.size() < range.end().offset()) {
      spdlog::error("Source text is smaller than the end offset");
      return std::nullopt;
    }

    // If no equal sign, abort
    if (std::find(txt.begin() + range.start().offset(), txt.begin() + range.end().offset(), '=') ==
        txt.begin() + range.end().offset()) {
      return std::nullopt;
    }

    auto chunk = txt.substr(range.start().offset(), range.end().offset() - range.start().offset());

    // Strip leading space
    chunk = chunk.substr(chunk.find_first_not_of(" \t\n\r\f\v"));
    // Strip first = sign
    if (chunk[0] == '=') {
      chunk = chunk.substr(1);
    }
    // Strip leading space again
    chunk = chunk.substr(chunk.find_first_not_of(" \t\n\r\f\v"));
    return std::string(chunk);
  }
  return std::nullopt;
}

// Given a list of syntax kinds, extract their sources ranges.
void Project::get_ranges_for_syntax_kinds(slang::syntax::SyntaxNode *node,
    const std::vector<slang::syntax::SyntaxKind> &kinds,
    std::vector<slang::SourceRange> &declarations) {
  if (node == nullptr)
    return;

  if (std::find(kinds.begin(), kinds.end(), node->kind) != kinds.end()) {
    declarations.push_back(node->sourceRange());
  }

  for (int idx = 0; idx < node->getChildCount(); idx++) {
    get_ranges_for_syntax_kinds(node->childNode(idx), kinds, declarations);
  }
}

bool Project::can_define_module(const fs::path &filepath, int line_idx, int col_idx) {
  if (!source_manager) {
    spdlog::error("Source manager not available");
    return false;
  }

  const int line = line_idx + 1;
  const int col = col_idx + 1;

  const auto syntax_tree =
      ss::SyntaxTree::fromFile(filepath.string(), *source_manager, {}, source_library.get());

  std::vector<slang::SourceRange> r;

  // Ideally, we would modify the AST and lean on the compiler to tell us if the modifications
  // were valid. However, I've seen the compiler fail to catch module-within-module declarations,
  // so we're taking this other hacky approach for now of checking ranges of declarations in the
  // AST.
  get_ranges_for_syntax_kinds(&syntax_tree.value()->root(),
      {slang::syntax::SyntaxKind::ModuleDeclaration,
          slang::syntax::SyntaxKind::ProgramDeclaration,
          slang::syntax::SyntaxKind::FunctionDeclaration,
          slang::syntax::SyntaxKind::GenerateBlock,
          slang::syntax::SyntaxKind::PackageDeclaration},
      r);

  for (const auto &range : r) {
    const size_t start_col = source_manager->getColumnNumber(range.start());
    const size_t start_line = source_manager->getLineNumber(range.start());

    const size_t end_col = source_manager->getColumnNumber(range.end());
    const size_t end_line = source_manager->getLineNumber(range.end());

    if (line >= start_line && line <= end_line) {
      if (line == start_line && col < start_col)
        continue;
      if (line == end_line && col > end_col)
        continue;
      return false;
    }
  }

  return true;
}

std::vector<ModuleDeclaration> Project::get_modules() {
  const auto compilation = compile();
  std::vector<ModuleDeclaration> res;

  if (!compilation.has_value()) {
    spdlog::error("Failed to compile project");
    return res;
  }

  auto defs = compilation.value()->getDefinitions();

  for (const auto &def : defs) {
    if (def->kind == slang::ast::SymbolKind::Definition) {
      auto &defsymbol = def->as<slang::ast::DefinitionSymbol>();

      if (defsymbol.definitionKind != slang::ast::DefinitionKind::Module)
        continue;

      // TODO: get rid of this artificial instance, just use the definition
      auto &inst = slang::ast::InstanceSymbol::createDefault(*compilation.value().get(), defsymbol);

      const auto &body = inst.as<slang::ast::InstanceSymbol>().body;
      const std::span<const slang::ast::Symbol *const> port_names = body.getPortList();

      // Add module
      ModuleDeclaration m;
      m.name = def->name;
      for (const auto &port_name : port_names) {
        m.ports.emplace_back(port_name->name);
      }

      auto find_parameter = [&m](std::string_view name) {
        return std::find_if(m.parameters.begin(), m.parameters.end(), [&name](const auto &p) {
          return p.first == name;
        }) != m.parameters.end();
      };

      for (const auto &param : defsymbol.parameters) {
        if (param.isLocalParam) {
          spdlog::debug("Local param: {}", param.valueSyntax->toString());
          continue;
        } else if (param.isTypeParam) {
          auto &type_param = param.typeSyntax->as<slang::syntax::TypeParameterDeclarationSyntax>();
          for (const auto &decl : type_param.declarators) {
            if (find_parameter(decl->name.valueText()))
              continue;  // Skip if parameter name exists

            spdlog::debug("Type param: {}", decl->name.valueText());

            m.parameters.push_back({std::string(decl->name.valueText()),
                decl->assignment ? extract_assigned_value(decl->assignment->sourceRange())
                                 : std::nullopt});
          }
        } else if (param.isPortParam) {
          spdlog::debug("Port param: {}", param.valueSyntax->toString());
          auto &value_param = param.valueSyntax->as<slang::syntax::ParameterDeclarationSyntax>();

          for (const auto &decl : value_param.declarators) {
            if (find_parameter(decl->name.valueText()))
              continue;  // Skip if parameter name exists

            m.parameters.push_back({std::string(decl->name.valueText()),
                decl->initializer ? extract_assigned_value(decl->initializer->sourceRange())
                                  : std::nullopt});
          }
        } else {
          spdlog::debug("Unknown parameter type");
        }
      }

      res.push_back(m);
    }
  }

  return res;
}

void Project::register_warning(std::string_view msg) {
  static constexpr size_t max_warnings = 10;
  if (compiler_warnings.size() < max_warnings) {
    if (compiler_warnings.find(msg) == compiler_warnings.end()) {
      compiler_warnings[msg] = false;
      spdlog::warn("Registered warning: {}", msg);
    }
  } else {
    spdlog::warn("Max warnings reached, skipping: {}", msg);
  }
}

std::vector<Location> Project::lookup(const fs::path &path, size_t row, size_t col) {
  // 0. Get root unit for path.
  // 1. Get symbol name first.
  // 2. Lookup symbol in AST.
  spdlog::info("Looking up symbol at: {}:{}:{}", path.string(), row, col);

  std::vector<Location> res;
  auto maybe_compilation = compile();
  if (!maybe_compilation.has_value()) {
    spdlog::error("Failed to compile project");
    return res;
  }

  // TODO: cache this.
  const auto syntax_trees = maybe_compilation.value()->getSyntaxTrees();
  LookupCacheVisitor visitor(maybe_compilation.value());
  for (const auto &tree : syntax_trees) {
    tree->root().visit(visitor);
  }

  // Find construct we are looking up.
  auto maybe_construct = visitor.lookup(
      path, row, col, {ConstructType::HIERARCHY_INSTANTIATION, ConstructType::INCLUDE_DIRECTIVE});

  if (maybe_construct.has_value()) {
    const auto [construct_type, construct_name] = maybe_construct.value();

    for (const auto &[_, unit] : root_units) {
      if (construct_type == ConstructType::INCLUDE_DIRECTIVE) {
        const auto &citr = unit->include_name_to_paths().find(construct_name);
        if (citr != unit->include_name_to_paths().end()) {
          for (const auto &p : citr->second) {
            res.push_back({p, Range{{0, 0}, {0, 0}}});
          }
        } else {
          spdlog::warn("Include directive not found: {}", construct_name);
        }
      } else {
        // Find definitions of construct.
        auto hits = visitor.lookup(construct_name, {ConstructType::MODULE_DECLARATION});
        spdlog::info("construct found: {}", construct_name);

        for (const auto &[type, loc] : hits) {
          spdlog::info("Construct type: {}", to_string(type));
          res.push_back(loc);
        }
      }
    }
  } else {
    spdlog::info("Construct not found");
  }
  return res;
}

// TODO: send warning messages
[[nodiscard]] std::optional<std::string_view> Project::add_root_unit(const fs::path &path) {
  // Check if path exists
  if (!fs::exists(path)) {
    spdlog::warn("Path does not exist: {}", path.string());
    return "Path does not exist"sv;
  }

  if (get_unit_via_path(path).has_value()) {
    spdlog::warn("Path is already within a compilation root: {}", path.string());
    return "Path is already within a compilation root"sv;
  }

  // Make sure path is not parent of any existing root unit
  for (const auto &[root_unit_path, root_unit] : root_units) {
    if (utils::is_path_part_of_path(root_unit_path, path)) {
      spdlog::warn("Path is parent of existing compilation root: {}", root_unit_path.string());
      return "Path is parent of existing compilation root"sv;
    }
  }

  // Create new compilation root and add it to the list of compilation roots.
  auto root_unit = RootUnit::create(path, false);
  root_units[path] = root_unit;
  scan_files();

  if (!write_dotfile()) {
    spdlog::error("Failed to write dotfile");
    return "Failed to write dotfile"sv;
  }

  return std::nullopt;
}

std::optional<std::string_view> /*error*/ Project::remove_root_unit(const fs::path &path) {
  // Check if path exists
  if (!fs::exists(path)) {
    spdlog::warn("Path does not exist: {}", path.string());
    return "Path does not exist"sv;
  }

  auto maybe_unit = get_unit_via_path(path);
  if (maybe_unit.has_value()) {
    if (maybe_unit.value()->principal()) {
      spdlog::warn("Cannot remove principal root unit: {}", path.string());
      return "Cannot remove principal root unit"sv;
    }

    root_units.erase(path);

    if (!write_dotfile()) {
      spdlog::error("Failed to write dotfile");
      return "Failed to write dotfile"sv;
    }
  } else {
    spdlog::warn("Path is not a compilation root: {}", path.string());
    return "Path is not a compilation root"sv;
  }

  return std::nullopt;
}
}  // namespace metalware
