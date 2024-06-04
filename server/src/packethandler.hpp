#pragma once

#include <filesystem>

#include "nlohmann/json.hpp"
#include "project.hpp"
#include "languageclient.hpp"


namespace metalware {
  static constexpr std::string_view PRODUCT_NAME = "HDL Copilot"sv;

  enum InsertTextFormat { PlainText = 1, Snippet = 2 };

  namespace fs = std::filesystem;

  struct Diagnostic;

  struct CompletionItemDetails {
    std::string detail;
    std::string_view description = PRODUCT_NAME;
  };

  struct TextEdit {
    Range range;
    std::string newText;

    [[nodiscard]] nlohmann::json to_json() const {
      return nlohmann::json {
          {"range", range.to_json()},
          {"newText", newText}
      };
    }
  };

  struct CompletionItem {
    std::string label;
    std::string_view kind;
    TextEdit textEdit;
    InsertTextFormat insertTextFormat;
    CompletionItemDetails details;

    [[nodiscard]] nlohmann::json to_json() const {
      return nlohmann::json {
          {"label", label},
          {"kind", kind},
          {"insertTextFormat", insertTextFormat},
          {"textEdit", textEdit.to_json()},
          {"labelDetails", {
            {"detail", details.detail},
            {"description", details.description}
          }}
      };
    }
  };

  struct CompletionList {
    std::vector<CompletionItem> items;
  };

  class PacketHandler {
    public:
      explicit PacketHandler(const std::weak_ptr<LanguageClient>& language_client);
      // HANDLERS
      [[nodiscard]] bool handle_json_message(const nlohmann::json &json_msg);
    private:
      [[nodiscard]] static CompletionList get_completions(
        std::string_view prefix, const fs::path &filepath, size_t line, size_t col);

      static bool is_utf8(std::string_view str);
      static std::string serialize_json_message(const nlohmann::json &json_msg);

      [[nodiscard]] bool handle_json_message_impl(const nlohmann::json &json_msg);
      [[nodiscard]] bool handle_include_resource(const nlohmann::json &json_msg) const;
      [[nodiscard]] bool handle_did_change(const nlohmann::json &json_msg);
      [[nodiscard]] bool handle_did_close(const nlohmann::json &json_msg);
      [[nodiscard]] bool handle_set_macros(const nlohmann::json &json_msg) const;
      [[nodiscard]] bool handle_initialize(const nlohmann::json &json_msg) const;
      [[nodiscard]] bool handle_did_open(const nlohmann::json &json_msg);
      [[nodiscard]] bool handle_definition(const nlohmann::json &json_msg) const;
      [[nodiscard]] bool handle_exclude_resource(const nlohmann::json &json_msg) const;
      [[nodiscard]] bool handle_text_document_completion(const nlohmann::json &json_msg) const;
      [[nodiscard]] bool handle_set_project_path(const nlohmann::json &json_msg);
      [[nodiscard]] bool handle_reload_dotfile(const nlohmann::json &json_msg);

      [[nodiscard]] bool handle_add_root_unit (const nlohmann::json &json_msg);
      [[nodiscard]] bool handle_remove_root_unit (const nlohmann::json &json_msg);

      [[nodiscard]] bool handle_set_license_key(const nlohmann::json &json_msg);
      [[nodiscard]] bool handle_get_diagnostic_strings_for_line(const nlohmann::json &json_msg) const;

      [[nodiscard]] bool send_license_missing() const;
      [[nodiscard]] bool send_license_invalid() const;
      [[nodiscard]] bool send_license_valid() const;
      [[nodiscard]] bool send_cache_license() const;
      [[nodiscard]] bool send_warning(std::string_view msg) const;
      [[nodiscard]] bool send_project_structure_changed() const;

      [[nodiscard]] bool send_diagnostics(const std::vector<Diagnostic> &all_diagnostics) const;

      [[nodiscard]] bool find_and_report_diagnostics();

      std::weak_ptr<LanguageClient> language_client_;
  };
}
