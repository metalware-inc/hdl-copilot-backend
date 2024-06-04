#include "packethandler.hpp"

#include "completions.hpp"
#include "languageclient.hpp"
#include "license.hpp"
#include "nlohmann/json.hpp"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"
#include "utils.hpp"

#define LICENSE_CHECK
#define DISABLE_LICENSING

namespace metalware {
std::optional<std::shared_ptr<Project>> current_project;

// HELPERS
std::string PacketHandler::serialize_json_message(const nlohmann::json &json_msg) {
  try {
    auto content = json_msg.dump(-1);
    std::string res = "Content-Length: " + std::to_string(content.size());
#if defined(_WIN32)
    res += "\n\n"sv;
#else
    res += "\r\n\r\n"sv;
#endif
    res += content;

    return res;
  } catch (nlohmann::json::type_error &e) {
    spdlog::error("Caught exception: {} in serializing", e.what());
  }
  return "";
}

bool PacketHandler::is_utf8(std::string_view str) {
  return std::all_of(str.begin(), str.end(), [](char c) {
    return (c & 0xC0) != 0x80;
  });
}

bool PacketHandler::send_project_structure_changed() const {
  if (!current_project.has_value())
    return false;

  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["method"] = "backend/projectStructureChanged";
  std::string resp = serialize_json_message(response);
  if (std::shared_ptr<LanguageClient> c = language_client_.lock())
    return c->send_packet(resp);
  return false;
}

bool PacketHandler::send_license_missing() const {
  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["method"] = "backend/licenseMissing";
  response["params"]["message"] = "License error: missing license";
  const std::string resp = serialize_json_message(response);

  if (std::shared_ptr<LanguageClient> c = language_client_.lock()) {
    return c->send_packet(resp);
  } else {
    spdlog::error("Failed to send missing license!");
  }
  return false;
}

bool PacketHandler::send_license_invalid() const {
  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["method"] = "backend/licenseInvalid";
  response["params"]["message"] = "License error: invalid license";
  auto resp = serialize_json_message(response);
  if (std::shared_ptr<LanguageClient> c = language_client_.lock())
    return c->send_packet(resp);
  return false;
}

bool PacketHandler::send_license_valid() const {
  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["method"] = "backend/licenseValid";
  response["params"]["message"] = "License is valid";
  std::string resp = serialize_json_message(response);
  if (std::shared_ptr<LanguageClient> c = language_client_.lock())
    return c->send_packet(resp);
  return false;
}

// The purpose of this method is to share a valid license key with the frontend.
bool PacketHandler::send_cache_license() const {
  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["method"] = "backend/cacheLicense";
  response["params"]["key"] = license::get_cached_license();
  std::string resp = serialize_json_message(response);
  if (std::shared_ptr<LanguageClient> c = language_client_.lock())
    return c->send_packet(resp);
  return false;
}

bool PacketHandler::send_warning(std::string_view msg) const {
  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["method"] = "backend/warning";
  response["params"]["type"] = 2;
  response["params"]["message"] = std::string(msg);
  std::string resp = serialize_json_message(response);
  if (std::shared_ptr<LanguageClient> c = language_client_.lock())
    return c->send_packet(resp);
  return false;
}

[[nodiscard]] bool PacketHandler::send_diagnostics(
    const std::vector<Diagnostic> &all_diagnostics) const {
  if (!current_project.has_value())
    return false;

  std::unordered_map<fs::path, std::vector<Diagnostic>> diagnostics_by_file;
  std::map<fs::path, std::vector<Diagnostic>> current_files_with_diagnostics;

  for (const auto &diag : all_diagnostics) {
    diagnostics_by_file[diag.filepath].push_back(diag);
    spdlog::debug(
        "New diagnostic raw {} uri {}", diag.filepath.string(), utils::path_to_uri(diag.filepath));

    if (current_files_with_diagnostics.find(diag.filepath) ==
        current_files_with_diagnostics.end()) {
      current_files_with_diagnostics[diag.filepath] = {};
    }

    current_files_with_diagnostics[diag.filepath].push_back(diag);
  }

  // Create empty diagnostics for files that had diagnostics in the past but not anymore. This is to
  // clear the diagnostics in the LSP client.
  for (const auto &[path, _] : current_project.value()->prev_files_with_diagnostics) {
    if (!diagnostics_by_file.contains(path)) {
      spdlog::debug(
          "Old diagnostic to clear raw {} uri {}", path.string(), utils::path_to_uri(path));
      diagnostics_by_file[path] = {};
    }
  }

  // Save the current files with diagnostics so they can be exonerated in the next call.
  current_project.value()->prev_files_with_diagnostics = current_files_with_diagnostics;

  // Send diagnostics for each file
  for (const auto &[filepath, file_diags] : diagnostics_by_file) {
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["method"] = "textDocument/publishDiagnostics";
    response["params"]["uri"] = utils::path_to_uri(filepath);
    spdlog::debug("The URI is: {}", response["params"]["uri"].get<std::string>());
    nlohmann::json diagnostics_json = nlohmann::json::array();

    for (const auto &diag : file_diags) {
      if (diag.severity == DiagnosticSeverity::None) {
        continue;
      }

      nlohmann::json diag_json;
      diag_json["message"] = diag.message;
      diag_json["severity"] = static_cast<int>(diag.severity);
      diag_json["range"]["start"]["line"] = diag.range.start.line;
      diag_json["range"]["start"]["character"] = diag.range.start.character;
      diag_json["range"]["end"]["line"] = diag.range.end.line;
      diag_json["range"]["end"]["character"] = diag.range.end.character;
      diag_json["source"] = "HDL Copilot";
      diagnostics_json.push_back(diag_json);
    }

    response["params"]["diagnostics"] = diagnostics_json;

    std::string resp = serialize_json_message(response);
    if (std::shared_ptr<LanguageClient> c = language_client_.lock())
      if (!c->send_packet(resp)) {
        return false;
      }
  }
  return true;
}

bool PacketHandler::find_and_report_diagnostics() {
  if (!current_project.has_value()) {
    spdlog::error("Find and report: No current project");
    return false;
  }

  LICENSE_CHECK

  std::vector<std::string_view> to_del;
  // Send unsent compiler warnings.
  for (auto &[msg, ack] : current_project.value()->compiler_warnings) {
    spdlog::warn("Compiler warning: {}", msg);
    if (!ack) {
      if (!send_warning(msg)) {
        spdlog::error("Failed to send warning: {}", msg);
      }
      if (std::find(REPETABLE_WARNINGS.begin(), REPETABLE_WARNINGS.end(), msg) ==
          REPETABLE_WARNINGS.end()) {
        current_project.value()->compiler_warnings[msg] = true;
      } else {
        to_del.push_back(msg);
      }
    }
  }

  for (const auto &msg : to_del) {
    current_project.value()->compiler_warnings.erase(msg);
  }

  // Time diagnostics
  auto last = std::chrono::high_resolution_clock::now();

  const auto lsp_diagnostics = current_project.value()->find_diagnostics();
  const bool res = send_diagnostics(lsp_diagnostics);

  spdlog::info("Time to find and send diagnostics: {}ms",
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::high_resolution_clock::now() - last)
          .count());
  return res;
}

CompletionList PacketHandler::get_completions(
    std::string_view prefix, const fs::path &filepath, size_t line, size_t col) {
  if (!current_project.has_value())
    return {};

  std::string trimmed = std::string(prefix);
  utils::ltrim(trimmed);
  utils::rtrim(trimmed);  // This is to remove newline (issue seen on Windows in VScode)

  CompletionList completion_list;

  // STATIC COMPLETIONS
  for (const auto &static_completion : static_completions) {
    const auto name = std::string(std::get<0>(static_completion));
    const auto insert_text = std::string(std::get<1>(static_completion));
    const auto description = std::string(std::get<2>(static_completion));

    if (name.starts_with(trimmed)) {
      spdlog::info("Considering static completion: {}", name);
      spdlog::info("Insert text: {}", insert_text);

      if (!is_utf8(name) || !is_utf8(insert_text)) {
        spdlog::warn("Skipping {} due to non-UTF8 characters", name);
        continue;
      }
      completion_list.items.push_back(CompletionItem{.label = name,
          .kind = "keyword",
          .textEdit = TextEdit{.range = Range{.start = Position{.line = line,
                                                  .character = col - trimmed.size()},
                                   .end = Position{.line = line, .character = col}},
              .newText = insert_text},
          .insertTextFormat = InsertTextFormat::Snippet,
          .details = CompletionItemDetails{.detail = " - " + description}});
    }
  }

  const auto modules = current_project.value()->get_modules();

  // MODULE INSTANTIATION
  // TODO: make sure we are within the right scope
  for (const auto &module : modules) {
    spdlog::debug("Considering module: {}", module.name);
    if (module.name.starts_with(trimmed)) {
      size_t running_param_index = 1;
      std::string module_instantiation = module.name;
      if (!module.parameters.empty()) {
        module_instantiation += " #(\n";
        for (size_t i = 0; i < module.parameters.size(); i++) {
          const auto &[param, default_value] = module.parameters.at(i);
          module_instantiation += "  ." + param + "(" + "${" +
                                  std::to_string(running_param_index++) + ":" +
                                  (default_value.has_value() ? default_value.value() : "") + "})";
          module_instantiation += (i != module.parameters.size() - 1) ? ",\n" : "\n";
        }
        module_instantiation += ") ";
      } else {
        module_instantiation += " ";
      }

      module_instantiation += "${" + std::to_string(running_param_index++) + ":instance_name} (\n";

      for (size_t i = 0; i < module.ports.size(); i++) {
        module_instantiation += "  ." + module.ports[i] + "(" + "${" +
                                std::to_string(i + running_param_index++) + ":" + module.ports[i] +
                                "})";
        module_instantiation += (i != module.ports.size() - 1) ? ",\n" : "\n";
      }

      module_instantiation += ");\n$0";

      // Only add if the modified source cod
      completion_list.items.push_back(CompletionItem{.label = module.name,
          .kind = "module",
          .textEdit = TextEdit{.range = Range{.start = Position{.line = line,
                                                  .character = col - trimmed.size()},
                                   .end = Position{.line = line, .character = col}},
              .newText = module_instantiation},
          .insertTextFormat = InsertTextFormat::Snippet,
          .details = CompletionItemDetails{.detail = " - Module instantiation"}});
    }
  }

  return completion_list;
}

// HANDLERS
bool PacketHandler::handle_initialize(const nlohmann::json &json_msg) const {
  spdlog::info("Received initialize request");
  // Make sure id is present
  if (!json_msg.contains("id")) {
    spdlog::error("Invalid initialize request: {}", json_msg.dump(4));
    return false;
  }

  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["id"] = json_msg["id"];

  response["result"]["capabilities"]["completionProvider"]["resolveProvider"] = false;
  response["result"]["capabilities"]["completionProvider"]["triggerCharacters"] = {"m", "p"};

  response["result"]["capabilities"]["codeActionProvider"] = false;
  response["result"]["capabilities"]["definitionProvider"] = true;

  response["result"]["capabilities"]["diagnosticProvider"]["interFileDependencies"] = false;
  response["result"]["capabilities"]["diagnosticProvider"]["workspaceDiagnostics"] = false;

  response["result"]["capabilities"]["documentFormattingProvider"] = false;
  response["result"]["capabilities"]["documentHighlightProvider"] = false;
  response["result"]["capabilities"]["documentSymbolProvider"] = false;

  response["result"]["capabilities"]["textDocumentSync"]["change"] = 1;
  response["result"]["capabilities"]["textDocumentSync"]["openClose"] = true;

  response["result"]["serverInfo"]["name"] = "HDL Copilot Server";
  response["result"]["serverInfo"]["version"] = std::to_string(VERSION);

  std::string resp = serialize_json_message(response);

  bool res = false;
  if (std::shared_ptr<LanguageClient> c = language_client_.lock()) {
    res = c->send_packet(resp);
  } else {
    spdlog::error("Get socket client failed");
  }

#ifndef DISABLE_LICENSING
  if (!license::read_license_file()) {
    spdlog::error("License file not found");
    if (!send_license_missing()) {
      spdlog::error("Failed to send license missing");
    }
  } else if (!license::is_valid()) {
    spdlog::error("Invalid license");
    if (!send_license_invalid()) {
      spdlog::error("Failed to send license invalid");
    }
  }
#endif
  return res;
}

bool PacketHandler::handle_definition(const nlohmann::json &json_msg) const {
  spdlog::info("Received definition request");

  if (!current_project.has_value())
    return false;

  if (!json_msg.contains("params") || !json_msg["params"].contains("position") ||
      !json_msg["params"]["position"].contains("line") ||
      !json_msg["params"]["position"].contains("character") ||
      !json_msg["params"].contains("textDocument") ||
      !json_msg["params"]["textDocument"].contains("uri")) {
    spdlog::error("Invalid definition request: {}", json_msg.dump(4));
    return false;
  }

  const auto path =
      utils::uri_to_path(json_msg["params"]["textDocument"]["uri"].get<std::string>());
  const auto row = json_msg["params"]["position"]["line"].get<size_t>();
  const auto col = json_msg["params"]["position"]["character"].get<size_t>();

  std::vector<Location> locations = current_project.value()->lookup(path, row, col);

  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["id"] = json_msg["id"];
  response["result"] = nlohmann::json::array();

  for (const auto &loc : locations) {
    response["result"].push_back(loc.to_json());
  }

  const auto resp = serialize_json_message(response);
  if (std::shared_ptr<LanguageClient> c = language_client_.lock())
    return c->send_packet(resp);

  return true;
}

bool PacketHandler::handle_did_open(const nlohmann::json &json_msg) {
  if (!current_project.has_value())
    return false;

  if (!json_msg.contains("params") || !json_msg["params"].contains("textDocument") ||
      !json_msg["params"]["textDocument"].contains("uri") ||
      !json_msg["params"]["textDocument"].contains("text")) {
    spdlog::error("Invalid didOpen request: {}", json_msg.dump(4));
    return false;
  }

  auto filepath = utils::uri_to_path(json_msg["params"]["textDocument"]["uri"].get<std::string>());
  const std::string text = json_msg["params"]["textDocument"]["text"];
  if (current_project.value()->add_file(filepath, text)) {
    return find_and_report_diagnostics();
  }

  return true;
}

bool PacketHandler::handle_include_resource(const nlohmann::json &json_msg) const {
  if (!current_project.has_value())
    return false;

  if (!json_msg.contains("params") || !json_msg["params"].contains("path")) {
    spdlog::error("Invalid includePath request: {}", json_msg.dump(4));
    return false;
  }

  std::string path = json_msg["params"]["path"].get<std::string>();
  utils::normalize_path(path);

  if (current_project.value()->include_resource({path})) {
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["method"] = "backend/exclusionsChanged";
    auto resp = serialize_json_message(response);
    if (std::shared_ptr<LanguageClient> c = language_client_.lock())
      return c->send_packet(resp);
  }
  return false;
}

bool PacketHandler::handle_exclude_resource(const nlohmann::json &json_msg) const {
  if (!current_project.has_value())
    return false;

  if (!json_msg.contains("params") || !json_msg["params"].contains("path")) {
    spdlog::error("Invalid excludePath request: {}", json_msg.dump(4));
    return false;
  }

  std::string path = json_msg["params"]["path"].get<std::string>();
  utils::normalize_path(path);

  if (current_project.value()->exclude_resource({path})) {
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["method"] = "backend/exclusionsChanged";
    auto resp = serialize_json_message(response);
    if (std::shared_ptr<LanguageClient> c = language_client_.lock())
      return c->send_packet(resp);
  }
  return true;
}

bool PacketHandler::handle_set_macros(const nlohmann::json &json_msg) const {
  if (!current_project.has_value())
    return false;

  if (!json_msg.contains("params") || !json_msg["params"].contains("macros") ||
      !json_msg["params"]["macros"].is_array()) {
    spdlog::error("Invalid defineMacro request: {}", json_msg.dump(4));
    return false;
  }

  std::vector<std::pair<std::string, std::string>> macros;

  for (const auto &macro : json_msg["params"]["macros"]) {
    if (!macro.contains("name") || !macro.contains("value")) {
      spdlog::error("Invalid macro: {}", macro.dump(4));
      return false;
    }

    macros.emplace_back(macro["name"].get<std::string>(), macro["value"].get<std::string>());
  }

  if (current_project.value()->set_macros(macros)) {
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["method"] = "backend/macrosChanged";
    auto resp = serialize_json_message(response);
    if (std::shared_ptr<LanguageClient> c = language_client_.lock())
      return c->send_packet(resp);
  }
  return true;
}

bool PacketHandler::handle_did_close(const nlohmann::json &json_msg) {
  LICENSE_CHECK
  if (!current_project.has_value())
    return false;

  if (!json_msg.contains("params") || !json_msg["params"].contains("textDocument") ||
      !json_msg["params"]["textDocument"].contains("uri")) {
    spdlog::error("Invalid didClose request: {}", json_msg.dump(4));
    return false;
  }

  auto filepath = utils::uri_to_path(json_msg["params"]["textDocument"]["uri"].get<std::string>());
  current_project.value()->remove_file_if_no_ent(filepath);

  return find_and_report_diagnostics();
}

bool PacketHandler::handle_text_document_completion(const nlohmann::json &json_msg) const {
  LICENSE_CHECK

  if (!current_project.has_value())
    return false;

  spdlog::debug("Received completion request {}", json_msg.dump(4));

  if (!json_msg.contains("params") || !json_msg["params"].contains("position") ||
      !json_msg["params"]["position"].contains("line") ||
      !json_msg["params"]["position"].contains("character") ||
      !json_msg["params"].contains("textDocument") ||
      !json_msg["params"]["textDocument"].contains("uri")) {
    return false;
  }

  // identify characters based on position
  const size_t col = json_msg["params"]["position"]["character"];
  const size_t line = json_msg["params"]["position"]["line"];

  fs::path filepath =
      utils::uri_to_path(json_msg["params"]["textDocument"]["uri"].get<std::string>());

  std::string buff;
  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["id"] = json_msg["id"];

  auto start = std::chrono::high_resolution_clock::now();
  if (current_project.value()->get_text_from_file_loc(filepath, line, col, buff)) {
    const auto completions = get_completions(buff, filepath, line, col);
    response["result"]["isIncomplete"] = false;
    response["result"]["items"] = nlohmann::json::array();
    for (const auto &completion : completions.items) {
      spdlog::debug("Completion: {}", completion.label);
      response["result"]["items"].push_back(completion.to_json());
    }
    auto end = std::chrono::high_resolution_clock::now();
    spdlog::info("Time to get {} completions: {}ms",
        completions.items.size(),
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    auto resp = serialize_json_message(response);
    if (std::shared_ptr<LanguageClient> c = language_client_.lock())
      return c->send_packet(resp);
  }

  return false;
}

bool PacketHandler::handle_did_change(const nlohmann::json &json_msg) {
  LICENSE_CHECK
  if (!current_project.has_value())
    return false;

  spdlog::info("Received didChange request");

  if (!json_msg.contains("params") || !json_msg["params"].contains("textDocument") ||
      !json_msg["params"]["textDocument"].contains("uri")) {
    spdlog::error("Invalid didChange request: {}", json_msg.dump(4));
    return false;
  }

  spdlog::info(" - uri: {}", json_msg["params"]["textDocument"]["uri"].get<std::string>());

  const std::string text = json_msg["params"]["contentChanges"][0]["text"];
  const fs::path filepath =
      utils::uri_to_path(json_msg["params"]["textDocument"]["uri"].get<std::string>());

  current_project.value()->update_file_buffer(filepath, text);
  return find_and_report_diagnostics();
}

bool PacketHandler::handle_add_root_unit(const nlohmann::json &json_msg) {
  if (!json_msg.contains("params") || !json_msg["params"].contains("path")) {
    spdlog::error("Invalid addDependentFolder request: {}", json_msg.dump(4));
    return false;
  }

  if (!current_project.has_value())
    return false;

  auto p = json_msg["params"]["path"].get<std::string>();
  utils::normalize_path(p);

  auto err_msg = current_project.value()->add_root_unit(p);

  if (err_msg.has_value()) {
    spdlog::error("Failed to add root unit folder: {}", err_msg.value());
    if (!send_warning(err_msg.value())) {
      return false;
    }
    return false;
  }

  if (!send_project_structure_changed()) {
    spdlog::error("Failed to send project structure changed");
  }

  return find_and_report_diagnostics();
}

bool PacketHandler::handle_remove_root_unit(const nlohmann::json &json_msg) {
  if (!json_msg.contains("params") || !json_msg["params"].contains("path")) {
    spdlog::error("Invalid removeDependentFolder request: {}", json_msg.dump(4));
    return false;
  }

  if (!current_project.has_value())
    return false;
  auto p = json_msg["params"]["path"].get<std::string>();
  utils::normalize_path(p);

  auto err_msg = current_project.value()->remove_root_unit(p);

  if (err_msg.has_value()) {
    spdlog::error("Failed to remove root unit folder: {}", err_msg.value());
    if (!send_warning(err_msg.value())) {
      return false;
    }
    return false;
  }

  if (!send_project_structure_changed()) {
    spdlog::error("Failed to send project structure changed");
  }

  return find_and_report_diagnostics();
}

bool PacketHandler::handle_set_license_key(const nlohmann::json &json_msg) {
  if (!json_msg.contains("params") || !json_msg["params"].contains("licenseKey")) {
    spdlog::error("Invalid setLicenseKey request: {}", json_msg.dump(4));
    return false;
  }

  const std::string license_key = json_msg["params"]["licenseKey"].get<std::string>();
  auto res = license::set_license_key(license_key);
  if (std::holds_alternative<license::LicenseError>(res)) {
    const auto err = std::get<license::LicenseError>(res);

    if (err.type == license::LicenseErrorType::InvalidKey) {
      spdlog::error("Invalid license key");
      if (!send_license_invalid()) {
        spdlog::error("Failed to send license invalid");
      }
    } else if (err.type == license::LicenseErrorType::FailedToWrite) {
      spdlog::error(err.message);
      if (!send_warning(err.message)) {
        spdlog::error("Failed to send warning");
      }
    }

    return false;
  } else {
    spdlog::info("License key set successfully");
    if (!send_license_valid()) {
      spdlog::error("Failed to send license valid");
    }
  }

  return find_and_report_diagnostics();
}

bool PacketHandler::handle_get_diagnostic_strings_for_line(const nlohmann::json &json_msg) const {
  if (!current_project.has_value())
    return false;

  if (!json_msg.contains("params") || !json_msg["params"].contains("filePath") ||
      !json_msg["params"].contains("line")) {
    spdlog::error("Invalid getDiagnosticStringsForLine request: {}", json_msg.dump(4));
    return false;
  }

  auto filepath = json_msg["params"]["filePath"].get<std::string>();
  utils::normalize_path(filepath);

  const size_t line = json_msg["params"]["line"].get<size_t>();

  // return should be a list of strings
  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["id"] = json_msg["id"];
  response["result"] = nlohmann::json::object();
  response["result"]["names"] = nlohmann::json::array();

  if (current_project.value()->prev_files_with_diagnostics.contains(filepath)) {
    for (const auto &diag : current_project.value()->prev_files_with_diagnostics.at(filepath)) {
      if (diag.range.start.line == line && std::find(response["result"]["names"].begin(),
                                               response["result"]["names"].end(),
                                               diag.name) == response["result"]["names"].end()) {
        response["result"]["names"].push_back(diag.name);
      }
    }
  } else {
    spdlog::warn("No diagnostics for file and line: {}:{}", filepath, line);
  }

  auto resp = serialize_json_message(response);
  if (std::shared_ptr<LanguageClient> c = language_client_.lock())
    return c->send_packet(resp);
  return true;
}

bool PacketHandler::handle_reload_dotfile(const nlohmann::json &json_msg) {
  if (!current_project.has_value())
    return false;

  if (!current_project.value()->load_dotfile()) {
    spdlog::error("Failed to reload dotfile");
    return false;
  }

  // Recompile the project
  return find_and_report_diagnostics();
}

bool PacketHandler::handle_set_project_path(const nlohmann::json &json_msg) {
  if (!json_msg.contains("params") || !json_msg["params"].contains("path")) {
    spdlog::error("Invalid setProjectPath request: {}", json_msg.dump(4));
    return false;
  }

  spdlog::info("Setting project path: {}", json_msg["params"]["path"].get<std::string>());

  if (!current_project.has_value()) {
    spdlog::info("Creating new project..");
  } else {
    spdlog::info("Removing current project..");
    current_project.value()->print_root_unit_paths();
    current_project.reset();
  }

  // path object represents the path to the project
  auto p = json_msg["params"]["path"].get<std::string>();
  utils::normalize_path(p);

  auto maybe_proj = Project::create(p);
  if (maybe_proj.has_value()) {
    current_project = maybe_proj.value();
    spdlog::debug("Now loading dotfile..");
    if (!current_project.value()->load_dotfile()) {
      spdlog::error("Failed to load dotfile");
      return false;
    }
  }

  return find_and_report_diagnostics();
}

bool PacketHandler::handle_json_message_impl(const nlohmann::json &json_msg) {
  if (json_msg.contains("method")) {
    std::string method = json_msg["method"];
    spdlog::info("Received JSON message: {}", method);

    if (method == "initialize") {
      return handle_initialize(json_msg);
    } else if (method == "initialized") {
      return find_and_report_diagnostics();
    } else if (method == "textDocument/completion") {
      return handle_text_document_completion(json_msg);
    } else if (method == "textDocument/didChange") {
      return handle_did_change(json_msg);
    } else if (method == "textDocument/didOpen") {
      return handle_did_open(json_msg);
    } else if (method == "textDocument/definition") {
      return handle_definition(json_msg);
    } else if (method == "includeResource") {
      return handle_include_resource(json_msg);
    } else if (method == "excludeResource") {
      return handle_exclude_resource(json_msg);
    } else if (method == "setMacros") {
      return handle_set_macros(json_msg);
    } else if (method == "recompile") {
      return find_and_report_diagnostics();
    } else if (method == "textDocument/didClose") {
      return handle_did_close(json_msg);
    } else if (method == "textDocument/didSave") {
      return true;
    } else if (method == "shutdown") {
      current_project.reset();
      return true;
    } else if (method == "$/setTrace") {
      return true;
    } else if (method == "setProjectPath") {
      return handle_set_project_path(json_msg);
    } else if (method == "reloadDotFile") {
      return handle_reload_dotfile(json_msg);
    } else if (method == "getDiagnosticStringsForLine") {
      return handle_get_diagnostic_strings_for_line(json_msg);
    } else if (method == "setLicenseKey") {
      return handle_set_license_key(json_msg);
    } else if (method == "compiler/addRootUnit") {
      return handle_add_root_unit(json_msg);
    } else if (method == "compiler/removeRootUnit") {
      return handle_remove_root_unit(json_msg);
    } else {
      spdlog::error("Unhandled method: {}", method);
      return true;
    }
  }
  return false;
}

PacketHandler::PacketHandler(const std::weak_ptr<LanguageClient> &language_client)
    : language_client_(language_client) {
  // check if socket client is valid
  if (language_client.expired()) {
    spdlog::error("Language client in init is expired");
  }
}

bool PacketHandler::handle_json_message(const nlohmann::json &json_msg) {
  bool res = handle_json_message_impl(json_msg);
  // If license is valid and it has not been shared with frontend, send it.
  if (current_project.has_value() && license::is_valid() &&
      !current_project.value()->license_shared_with_client) {
    spdlog::debug("Sharing license with frontend");
    current_project.value()->license_shared_with_client = true;
    return send_cache_license() && res;
  }
  return res;
}

}  // namespace metalware
