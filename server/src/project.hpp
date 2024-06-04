#pragma once

#include "slang/text/SourceManager.h"
#include "slang/ast/Compilation.h"

#include <filesystem>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "rootunit.hpp"

#include "shared.hpp"

namespace fs = std::filesystem;

namespace metalware {
  static constexpr std::string_view DOT_FILENAME = ".hdl-project"sv;

  namespace lsp {
    struct Diagnostic; // forward declaration
  }

  static constexpr std::string_view WARNING_EXCEEDS_MAX_FILE_COUNT =
    "Exceeded max files (10000) for project. Consider excluding unneeded files from compilation."sv;

  // Warnings that are sticky are broadcast every time they occur.
  static constexpr auto REPETABLE_WARNINGS = {
    WARNING_EXCEEDS_MAX_FILE_COUNT
  };

  class Project {
    Project(const fs::path &path) : principal_root_unit(RootUnit::create(path, true)) {
      root_units[path] = principal_root_unit;
    }

    Project(Project const&) = delete;
    Project& operator=(Project const&) = delete;
    Project(Project&&) = delete;
    Project& operator=(Project&&) = delete;


    std::vector<Diagnostic> suppressed_diagnostics;  // project-wide suppressions
    std::map<fs::path, RootUnitPtr> root_units;
    RootUnitPtr principal_root_unit;

    std::vector<std::string> defines; // aka macros

    std::vector<fs::path> excluded_paths = {}; // paths that should be excluded

    std::vector<std::string> non_inlined_fp_string_cache = {};
    std::unordered_map<fs::path, int> fp_ranks = {};

    // Should these really be members?
    std::shared_ptr<slang::SourceManager> source_manager = nullptr;
    std::shared_ptr<slang::SourceLibrary> source_library = nullptr;

    std::optional<std::shared_ptr<slang::ast::Compilation>> cached_compilation = std::nullopt;

    // Methods
    [[nodiscard]] std::optional<std::string> extract_assigned_value(slang::SourceRange range);
    [[nodiscard]] nonstd::expected<std::shared_ptr<slang::ast::Compilation>, std::string> compile();
    [[nodiscard]] bool add_target_files_to_compilation(const std::vector<fs::path> &target_file_paths,
        const std::shared_ptr<slang::ast::Compilation>& compilation);

    [[nodiscard]] std::optional<RootUnitPtr> get_unit_via_path(const fs::path &path) const;

    void scan_files();

    [[nodiscard]] bool can_define_module (const fs::path& filepath, int line, int col);

    void exclude_rel_paths(const std::vector<std::string>& relative_paths);

    void register_warning(std::string_view msg);
    void acknowledge_warning(std::string_view msg);

    void get_ranges_for_syntax_kinds (slang::syntax::SyntaxNode* node,
        const std::vector<slang::syntax::SyntaxKind> &kinds,
        std::vector<slang::SourceRange> &declarations);
public:
    static nonstd::expected<std::shared_ptr<Project>, std::string_view> create(const fs::path &path);

    void print_root_unit_paths();

    [[nodiscard]] std::vector<Diagnostic> find_diagnostics();
    [[nodiscard]] std::vector<ModuleDeclaration> get_modules();
    [[nodiscard]] bool load_dotfile(bool detect_noninlined_files = true);
    [[nodiscard]] bool write_dotfile();
    [[nodiscard]] bool get_text_from_file_loc(
        const fs::path& path, int line, int col, std::string &text) const;
    void update_file_buffer(const fs::path& filepath, const std::string &buff);

    bool add_file(const fs::path &path, const std::string &buff);
    void remove_file_if_no_ent(const fs::path &path);

    [[nodiscard]] bool is_resource_excluded(const fs::path &path);
    [[nodiscard]] bool exclude_resource(const fs::path &path);
    [[nodiscard]] bool include_resource(const fs::path &path);

    [[nodiscard]] bool set_macros(const std::vector<std::pair<std::string, std::string>>& macros);
    [[nodiscard]] std::optional<std::string_view> /*error*/ add_root_unit(const fs::path &path);
    [[nodiscard]] std::optional<std::string_view> /*error*/ remove_root_unit(const fs::path &path);

    std::vector<Location> lookup(const fs::path &path, size_t row, size_t col);

    std::map<fs::path, std::vector<Diagnostic>> prev_files_with_diagnostics;
    std::map</*msg*/ std::string_view, /*ack*/ bool> compiler_warnings;

    void set_fp_rank(const fs::path& p, int rank);
    int get_fp_rank(const fs::path& p);

    bool license_shared_with_client = false;

    ~Project() = default;
  };
}
