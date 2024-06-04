#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <vector>

#include "project.hpp"
#include "rootunit.hpp"
#include "shared.hpp"
#include "spdlog/spdlog.h"
#include "utils.hpp"

using namespace metalware;

static void write_dotfile(const nlohmann::json& dotfile, const fs::path& project_directory) {
  const fs::path dotfile_path = project_directory / DOT_FILENAME;
  std::ofstream ofs(dotfile_path);
  ofs << dotfile.dump();
  ofs.close();
}

TEST_CASE("Path Exclude-Include", "[path_exc_inc],[exc_inc]") {
  SECTION("Exclude Path 1") {
    const std::vector<fs::path> exclude_paths1 = {"/foo/bar"};
    const fs::path path1 = "/foo/bar";        // excluded
    const fs::path path2 = "/foo/bar/cool";   // excluded
    const fs::path path3 = "/foo/bar/cool/";  // excluded
    const fs::path path4 = "/foo/bar2";       // included
    REQUIRE(utils::is_path_excluded(path1, exclude_paths1));
    REQUIRE(utils::is_path_excluded(path2, exclude_paths1));
    REQUIRE(utils::is_path_excluded(path3, exclude_paths1));
    REQUIRE_FALSE(utils::is_path_excluded(path4, exclude_paths1));
  }
  SECTION("Exclude Path 2") {
    const std::vector<fs::path> exclude_paths2 = {"/foo/cool/name"};
    const fs::path path5 = "/foo/cool/name1";        // included
    const fs::path path6 = "/foo/cool/name2";        // included
    const fs::path path7 = "/foo/cool/name";         // excluded
    const fs::path path8 = "/foo/cool/name/drinks";  // excluded
    REQUIRE_FALSE(utils::is_path_excluded(path5, exclude_paths2));
    REQUIRE_FALSE(utils::is_path_excluded(path6, exclude_paths2));
    REQUIRE(utils::is_path_excluded(path7, exclude_paths2));
    REQUIRE(utils::is_path_excluded(path8, exclude_paths2));
  }
  SECTION("Exclude Path 3") {
    const std::vector<fs::path> exclude_paths3 = {"/elon/husk22"};
    const fs::path path9 = "/elon/husk";     // included
    const fs::path path10 = "/elon/husk22";  // excluded
    REQUIRE_FALSE(utils::is_path_excluded(path9, exclude_paths3));
    REQUIRE(utils::is_path_excluded(path10, exclude_paths3));
  }
}

TEST_CASE("Resource Exclude-Include", "[resource_exc_inc],[exc_inc]") {
  const fs::path root_directory = fs::absolute("tests/projects/tree");
  write_dotfile({}, root_directory);

  auto maybe_project = Project::create(root_directory);
  REQUIRE(maybe_project.has_value());
  const auto project = maybe_project.value();

  // Create paths
  const fs::path foo1 = root_directory / "foo1";
  const fs::path foo2 = root_directory / "foo2";
  const fs::path foo1_dumb = foo1 / "dumb";
  const fs::path foo1_lame = foo1 / "lame";
  const fs::path foo1_ok = foo1 / "ok";
  const fs::path foo2_dumb2 = foo2 / "dumb2";
  const fs::path foo2_lame2 = foo2 / "lame2";

  // Reusable lambdas
  auto ensure_foo1_and_children_inclusion = [&]() {
    REQUIRE_FALSE(project->is_resource_excluded(foo1));
    REQUIRE_FALSE(project->is_resource_excluded(foo1_dumb));
    REQUIRE_FALSE(project->is_resource_excluded(foo1_lame));
    REQUIRE_FALSE(project->is_resource_excluded(foo1_ok));
  };
  auto ensure_foo1_and_children_exclusion = [&]() {
    REQUIRE(project->is_resource_excluded(foo1));
    REQUIRE(project->is_resource_excluded(foo1_dumb));
    REQUIRE(project->is_resource_excluded(foo1_lame));
    REQUIRE(project->is_resource_excluded(foo1_ok));
  };
  auto ensure_foo2_and_children_inclusion = [&]() {
    REQUIRE_FALSE(project->is_resource_excluded(foo2));
    REQUIRE_FALSE(project->is_resource_excluded(foo2_dumb2));
    REQUIRE_FALSE(project->is_resource_excluded(foo2_lame2));
  };
  auto ensure_foo2_and_children_exclusion = [&]() {
    REQUIRE(project->is_resource_excluded(foo2));
    REQUIRE(project->is_resource_excluded(foo2_dumb2));
    REQUIRE(project->is_resource_excluded(foo2_lame2));
  };

  // Initially exclude all resources
  REQUIRE(project->exclude_resource(root_directory));
  REQUIRE(project->is_resource_excluded(root_directory));

  SECTION("Include File") {
    REQUIRE(project->is_resource_excluded(foo1_lame));

    // Include ./tree/foo1/lame
    REQUIRE(project->include_resource(foo1_lame));
    REQUIRE_FALSE(project->is_resource_excluded(foo1_lame));
    REQUIRE_FALSE(project->is_resource_excluded(foo1));
    REQUIRE_FALSE(project->is_resource_excluded(root_directory));

    // Verify all other resources are excluded
    REQUIRE(project->is_resource_excluded(foo1_ok));
    REQUIRE(project->is_resource_excluded(foo1_dumb));
    ensure_foo2_and_children_exclusion();
  }
  SECTION("Include Directory") {
    REQUIRE(project->is_resource_excluded(foo2));

    // Include ./tree/foo2, which includes foo2's children and parent
    REQUIRE(project->include_resource(foo2));
    ensure_foo2_and_children_inclusion();
    REQUIRE_FALSE(project->is_resource_excluded(root_directory));

    // Sibling directories should remain excluded
    REQUIRE(project->is_resource_excluded(foo1));
    REQUIRE(project->is_resource_excluded(foo1_lame));

    // Include the root and check tree is included
    REQUIRE(project->include_resource(root_directory));
    REQUIRE_FALSE(project->is_resource_excluded(root_directory));
    ensure_foo1_and_children_inclusion();
    ensure_foo2_and_children_inclusion();
  }
  SECTION("Parent Include Causes Child Inclusion") {
    // Check all children are excluded
    ensure_foo1_and_children_exclusion();
    ensure_foo2_and_children_exclusion();

    // Include parent and verify children are included
    REQUIRE(project->include_resource(foo1));
    ensure_foo1_and_children_inclusion();

    REQUIRE(project->include_resource(root_directory));
    ensure_foo1_and_children_inclusion();
    ensure_foo2_and_children_inclusion();
  }
}

TEST_CASE("External Resource Exclude-Include", "[external_resource_exc_inc],[exc_inc]") {
  const fs::path root_directory = "tests/projects/tree";
  write_dotfile({}, root_directory);

  auto maybe_project = Project::create(root_directory);
  REQUIRE(maybe_project.has_value());
  const auto project = maybe_project.value();

  const fs::path out_of_project = "tests/projects/ok_include";

  SECTION("Exclude External Resource") {
    REQUIRE_FALSE(project->exclude_resource(out_of_project));
    REQUIRE_FALSE(project->is_resource_excluded(out_of_project));
  }
  SECTION("Include External Resource") {
    REQUIRE_FALSE(project->include_resource(out_of_project));
  }
}

TEST_CASE("UVM Files", "[uvm_files],[uvm]") {
  const fs::path root_directory = "tests/projects/uvm-1.2/";
  RootUnitPtr root_unit = RootUnit::create(root_directory);
  REQUIRE(root_unit->scan_files({}) == ScanResult::Success);
  const auto noninlined_files = root_unit->non_inlined_files();
  const auto inlined_files =
      std::set<fs::path>(root_unit->inlined_files().begin(), root_unit->inlined_files().end());
  const auto header_files = root_unit->header_files();
  const auto include_name_to_paths = root_unit->include_name_to_paths();

  std::vector<fs::path> expected_noninlined_files = {
      "tests/projects/uvm-1.2/examples/integrated/ubus/examples/ubus_tb_top.sv",
      "tests/projects/uvm-1.2/src/uvm.sv",
  };
  std::set<fs::path> expected_inlined_files = {
      "tests/projects/uvm-1.2/src/uvm_macros.svh",  // uvm_pkg.sv includes uvm_macros.svh
      "tests/projects/uvm-1.2/src/uvm_pkg.sv",      // uvm.sv includes uvm_pkg.sv
      "tests/projects/uvm-1.2/src/reg/sequences/uvm_reg_access_seq.svh"  // included in
                                                                         // uvm_reg_model.svh
  };
  std::vector<fs::path> expected_header_files = {"tests/projects/uvm-1.2/src/uvm_macros.svh"};
  std::map<std::string, std::set<fs::path>> expected_include_name_to_paths = {
      {"uvm_pkg.sv", {"tests/projects/uvm-1.2/src/uvm_pkg.sv"}}};

  for (const auto& file : expected_noninlined_files) {
    CHECK(std::find(noninlined_files.begin(), noninlined_files.end(), file) !=
          noninlined_files.end());
  }
  for (const auto& file : expected_inlined_files) {
    CHECK(inlined_files.find(file) != inlined_files.end());
  }
  for (const auto& file : expected_header_files) {
    CHECK(std::find(header_files.begin(), header_files.end(), file) != header_files.end());
  }

  // Check include name to paths
  for (const auto& [name, paths] : expected_include_name_to_paths) {
    auto it = include_name_to_paths.find(name);
    REQUIRE(it != include_name_to_paths.end());
    for (const auto& path : paths) {
      CHECK(it->second.find(path) != it->second.end());
    }
  }
}

TEST_CASE("UVM Errors", "[uvm_errors],[uvm],[diagnostics]") {
  const fs::path root_directory = "tests/projects/uvm-1.2/examples/integrated/ubus";
  write_dotfile({}, root_directory);

  const auto maybe_project = Project::create(root_directory);
  REQUIRE(maybe_project.has_value());
  const auto& project = maybe_project.value();

  const auto maybe_err = project->add_root_unit("tests/projects/uvm-1.2/src");
  REQUIRE_FALSE(maybe_err.has_value());

  auto diagnostics = project->find_diagnostics();
  REQUIRE(!diagnostics.empty());

  size_t non_errors = 0;
  bool found_errors = false;
  for (const auto& diag : diagnostics) {
    if (diag.severity == DiagnosticSeverity::Error) {
      spdlog::error("Error: {} {} {}:{}",
          diag.message,
          diag.filepath.string(),
          diag.range.start.line,
          diag.range.start.character);
      found_errors = true;
    } else {
      non_errors++;
    }
  }

  spdlog::info("Found {} non-error diagnostics", non_errors);
  REQUIRE_FALSE(found_errors);
}

TEST_CASE("UVM compilation time", "[timing],[uvm]") {
  const fs::path root_directory = "tests/projects/uvm-1.2/examples/integrated/ubus";
  write_dotfile({}, root_directory);

  const auto start_time = std::chrono::high_resolution_clock::now();

  const auto maybe_project = Project::create(root_directory);
  REQUIRE(maybe_project.has_value());
  const auto& project = maybe_project.value();

  const auto maybe_err = project->add_root_unit("tests/projects/uvm-1.2/src");
  REQUIRE_FALSE(maybe_err.has_value());

  const auto& diagnostics = project->find_diagnostics();
  REQUIRE(!diagnostics.empty());

  const auto end_time = std::chrono::high_resolution_clock::now();

  REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() <
          500 /*ms*/);
}

TEST_CASE("Inexistant Includes", "[broken_includes],[includes],[diagnostics]") {
  const fs::path root_directory = "tests/projects/broken_include";
  write_dotfile({}, root_directory);

  auto maybe_project = Project::create(root_directory);
  REQUIRE(maybe_project.has_value());
  const auto project = maybe_project.value();

  auto diagnostics = project->find_diagnostics();
  REQUIRE(!diagnostics.empty());

  const std::vector<std::string> expected_errors = {
      "'inexistent.sv': no such file or directory",
      "unknown macro or compiler directive '`inexistent'",
  };

  // Convert diagnostic messages to lowercase
  std::vector<std::string> diagnostic_errors_lowered;
  for (const auto& diag : diagnostics) {
    if (diag.severity == DiagnosticSeverity::Error) {
      std::string error_lowered = diag.message;
      std::transform(
          error_lowered.begin(), error_lowered.end(), error_lowered.begin(), [](unsigned char c) {
            return std::tolower(c);
          });
      diagnostic_errors_lowered.push_back(error_lowered);
      spdlog::info("Diagnostic: {}", error_lowered);
    }
  }

  // Require each expected error to be found in the diagnostic messages
  for (const auto& expected_error : expected_errors) {
    auto it = std::find(
        diagnostic_errors_lowered.begin(), diagnostic_errors_lowered.end(), expected_error);
    REQUIRE(it != diagnostic_errors_lowered.end());
  }
  REQUIRE(diagnostics.size() == 2);
}

TEST_CASE("Valid Includes", "[valid_includes],[includes],[diagnostics]") {
  const fs::path root_directory = "tests/projects/ok_include";
  write_dotfile({}, root_directory);

  auto maybe_project = Project::create(root_directory);
  REQUIRE(maybe_project.has_value());
  const auto project = maybe_project.value();

  REQUIRE(project->find_diagnostics().empty());
}

TEST_CASE("File Level Duplicate Definitions",
    "[file_level_duplicate_definitions],[file_level],[diagnostics]") {
  const fs::path root_directory = "tests/projects/redefinitions";
  write_dotfile({}, root_directory);

  auto maybe_project = Project::create(root_directory);
  REQUIRE(maybe_project.has_value());
  const auto project = maybe_project.value();

  auto diagnostics = project->find_diagnostics();
  REQUIRE(!diagnostics.empty());

  // Expecting one specific diagnostic.
  static constexpr std::string_view expected_error = "DuplicateDefinition";
  bool found = false;
  for (const auto& diag : diagnostics) {
    if (diag.name.find(expected_error) != std::string::npos) {
      found = true;
    }
    REQUIRE_FALSE(diag.severity == DiagnosticSeverity::Error);
  }
  REQUIRE(found);
}

TEST_CASE("File Level Exclusion", "[file_level_exclusion],[file_level],[diagnostics]") {
  const fs::path root_directory = "tests/projects/redefinitions";
  nlohmann::json dotfile = {{"excludePaths", {"foo1.sv"}}};
  write_dotfile(dotfile, root_directory);

  auto maybe_project = Project::create(root_directory);
  REQUIRE(maybe_project.has_value());
  const auto project = maybe_project.value();

  auto diagnostics = project->find_diagnostics();
  static constexpr std::string_view unexpected_error = "DuplicateDefinition";
  for (const auto& diag : diagnostics) {
    REQUIRE_FALSE(diag.name.find(unexpected_error) != std::string::npos);
    REQUIRE_FALSE(diag.severity == DiagnosticSeverity::Error);
  }
}

TEST_CASE("Folder Level Exclusion Diagnostics",
    "[folder_level_exclusion_diags],[folder_level],[diagnostics]") {
  const fs::path root_directory = "tests/projects/exclusions";
  write_dotfile({}, root_directory);

  auto maybe_project = Project::create(root_directory);
  REQUIRE(maybe_project.has_value());
  const auto project = maybe_project.value();

  auto diagnostics = project->find_diagnostics();
  REQUIRE(!diagnostics.empty());
}

TEST_CASE("Folder Level Exclusion No Diagnostics",
    "[folder_level_exclusion_no_diags],[folder_level],[diagnostics]") {
  const fs::path root_directory = "tests/projects/exclusions";
  nlohmann::json dotfile = {{"excludePaths", {"level_foo/foo2"}}};
  write_dotfile(dotfile, root_directory);

  auto maybe_project = Project::create(root_directory);
  REQUIRE(maybe_project.has_value());
  const auto project = maybe_project.value();

  // Force compilation order
  project->set_fp_rank(root_directory / "main_tb.sv", 0);
  auto diagnostics = project->find_diagnostics();
  for (const auto& diag : diagnostics) {
    spdlog::error("Diagnostic: {}", diag.message.c_str());
  }
  REQUIRE(diagnostics.empty());
}
