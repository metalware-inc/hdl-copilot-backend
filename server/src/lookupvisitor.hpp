#pragma once
#include "slang/syntax/SyntaxVisitor.h"
#include "slang/parsing/Parser.h"
#include "slang/parsing/ParserMetadata.h"
#include "slang/parsing/Preprocessor.h"

#include "spdlog/spdlog.h"

namespace metalware {
class LookupCacheVisitor : public slang::syntax::SyntaxVisitor<LookupCacheVisitor> {
 private:
  std::shared_ptr<slang::ast::Compilation> compilation;

  using NameAndLocation = std::tuple<std::string, Location>;
  using PathToVecOfNameAndLocation = std::unordered_map<fs::path, std::vector<NameAndLocation>>;

  // Caches for finding constructs by type, path, name and location.
  std::unordered_map<ConstructType, PathToVecOfNameAndLocation> constructs;

 public:
  explicit LookupCacheVisitor(std::shared_ptr<slang::ast::Compilation> compilation)
      : compilation(compilation) {}

  void visitToken(slang::parsing::Token token) {
    const auto source_manager = compilation->getSourceManager();

    for (const auto &trivia : token.trivia()) {
      if (trivia.kind == slang::parsing::TriviaKind::Directive) {
        if (trivia.syntax() != nullptr &&
            trivia.syntax()->kind == slang::syntax::SyntaxKind::IncludeDirective) {
          const auto syntax = static_cast<slang::syntax::IncludeDirectiveSyntax *>(trivia.syntax());

          const size_t start_line_idx =
              source_manager->getLineNumber(syntax->sourceRange().start()) - 1;
          const size_t start_column_idx =
              source_manager->getColumnNumber(syntax->sourceRange().start()) - 1;
          const size_t end_line_idx =
              source_manager->getLineNumber(syntax->sourceRange().end()) - 1;
          const size_t end_column_idx =
              source_manager->getColumnNumber(syntax->sourceRange().end()) - 1;

          const fs::path path = source_manager->getFullPath(syntax->sourceRange().start().buffer());

          spdlog::debug("Found include directive at path: {} {}:{}-{}:{}",
              path.string(),
              start_line_idx,
              start_column_idx,
              end_line_idx,
              end_column_idx);

          auto fileName = std::string(syntax->fileName.valueText());
          // Remove quotes if present
          if (fileName.front() == '"' && fileName.back() == '"') {
            fileName = fileName.substr(1, fileName.size() - 2);
          }

          constructs[ConstructType::INCLUDE_DIRECTIVE][path].push_back(std::make_tuple(fileName,
              Location{
                  path,
                  Range{{start_line_idx, start_column_idx}, {end_line_idx, end_column_idx}},
              }));
          spdlog::debug(" File name is {}", fileName);
        }
      }
    }
  }

  // Finds constructs by name and type.
  std::vector<std::tuple<ConstructType, Location>> lookup(
      std::string_view name, std::initializer_list<ConstructType> types) {
    std::vector<std::tuple<ConstructType, Location>> res;

    // O(T*M*P), T=number of types, N=max number of constructs in a file, P=number of paths
    for (const auto &t : types) {
      // O(1)
      if (constructs.find(t) == constructs.end()) {
        continue;
      }

      // O(P*N), P=number of paths, N=number of constructs
      for (const auto &[path, v] : constructs.at(t)) {
        for (const auto &[n, loc] : v) {
          if (n == name) {
            res.emplace_back(t, loc);
          }
        }
      }
    }

    return res;
  }

  std::optional<std::tuple<ConstructType, /*name*/ std::string>> lookup(const fs::path &p,
      size_t row_idx,
      size_t col_idx,
      std::initializer_list<ConstructType> types) {
    // O(T*M), T=number of types, M=max number of constructs in a file
    for (const auto &t : types) {
      if (constructs.find(t) == constructs.end()) {  // O(1)
        continue;
      }

      const auto &typed_constructs = constructs.at(t);
      if (typed_constructs.find(p) == typed_constructs.end()) {  // O(1)
        continue;
      }

      const auto &typed_constructs_for_path = typed_constructs.at(p);

      for (const auto &[n, loc] : typed_constructs_for_path) {
        spdlog::debug("Checking construct: {} at {}:{}-{}:{}",
            n,
            loc.range.start.line,
            loc.range.start.character,
            loc.range.end.line,
            loc.range.end.character);
        const auto &r = loc.range;
        // This only works for constructs that are on the same line.
        // TODO: handle constructs that span multiple lines.
        if (row_idx == r.start.line && col_idx >= r.start.character && row_idx == r.end.line &&
            col_idx <= r.end.character) {
          return std::make_tuple(t, n);
        }
      }
    }

    return std::nullopt;
  }

  void handle(const slang::syntax::ModuleDeclarationSyntax &syntax) {
    const auto source_manager = compilation->getSourceManager();
    const size_t start_line_idx = source_manager->getLineNumber(syntax.sourceRange().start()) - 1;
    const size_t start_column_idx =
        source_manager->getColumnNumber(syntax.sourceRange().start()) - 1;
    const size_t end_line_idx = source_manager->getLineNumber(syntax.sourceRange().end()) - 1;
    const size_t end_column_idx = source_manager->getColumnNumber(syntax.sourceRange().end()) - 1;

    const fs::path path = source_manager->getFullPath(syntax.sourceRange().start().buffer());

    constructs[ConstructType::MODULE_DECLARATION][path].emplace_back(std::string(syntax.header->name.valueText()),
            Location{
                path,
                Range{{start_line_idx, start_column_idx}, {end_line_idx, end_column_idx}},
            });

    visitDefault(syntax);
  }

  void handle(const slang::syntax::HierarchyInstantiationSyntax &syntax) {
    // Example. Input: FIFO x (param1,  param2) Output: FIFO
    const auto source_manager = compilation->getSourceManager();

    // Make zero-indexed (slang stores 1-indexed).
    const size_t start_line_idx = source_manager->getLineNumber(syntax.sourceRange().start()) - 1;
    const size_t start_column_idx =
        source_manager->getColumnNumber(syntax.sourceRange().start()) - 1;

    // Just capture the position of the name of the hierarchy (i.e module) being instantiated.
    const size_t end_line_idx = start_line_idx;
    const size_t end_column_idx = start_column_idx + syntax.type.valueText().size();

    const fs::path path = source_manager->getFullPath(syntax.sourceRange().start().buffer());

    constructs[ConstructType::HIERARCHY_INSTANTIATION][path].emplace_back(std::string(syntax.type.valueText()),
            Location{
                path,
                Range{{start_line_idx, start_column_idx}, {end_line_idx, end_column_idx}},
            });

    visitDefault(syntax);
  }
};
}
