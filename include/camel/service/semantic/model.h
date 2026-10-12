/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The semantic model of a program: what one compilation knows about its source, kept so that
 * it can be asked by position (docs/technical/32_semantic_query_layer.md).
 *
 * analyze() compiles a source text exactly as `camel check` does, with a recorder attached
 * (camel/compile/semantic_recorder.h), and joins what the builders reported to the syntactic
 * index of each file involved (syntax_index.h): the entry file and every module it imports
 * from source. The result is self-contained (texts, ranges, strings): the compilation it
 * came from is gone when analyze() returns.
 *
 * Positions are byte offsets into the UTF-8 text of a file; LineIndex converts to and from
 * the line / UTF-16 column pairs of the language server protocol and the line / code point
 * pairs of the compiler's diagnostics.
 *
 * Nothing here knows the protocol: tools/lang-server maps these answers to LSP messages.
 */

#pragma once

#include "camel/service/semantic/syntax_index.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::service::semantic {

/// Line / column arithmetic on one text.
class LineIndex {
  public:
    LineIndex() = default;
    explicit LineIndex(std::string_view text);

    struct Position {
        uint32_t line      = 0;
        uint32_t character = 0;
    };

    size_t lineCount() const { return lineStarts_.size(); }
    /// Byte offset of a line / UTF-16 column pair; out-of-range values are clamped to the end
    /// of the line or of the text.
    uint32_t offsetOfUtf16(uint32_t line, uint32_t character) const;
    /// Byte offset of a line / code point column pair (the compiler's columns).
    uint32_t offsetOfCodePoint(uint32_t line, uint32_t character) const;
    /// Line / UTF-16 column of a byte offset.
    Position utf16PositionOf(uint32_t offset) const;

  private:
    std::string_view text_;
    std::vector<uint32_t> lineStarts_ = {0};
    uint32_t lineEnd(uint32_t line) const;
};

/// A type as it is written in source (`int`, not `i64`), with what a dot can select from it.
struct TypeInfo {
    std::string text;
    /// Struct fields, or tuple elements named "0", "1", ...: name and index into the model's
    /// types.
    std::vector<std::pair<std::string, uint32_t>> fields;
};

struct Location {
    uint32_t file = 0;
    Range range;
};

struct DiagnosticInfo {
    uint32_t file = 0; // index into the model's files
    Range range;
    int severity = 1; // 1 error, 2 warning, 3 information, 4 hint
    std::string code;       // "0x........"
    std::string name;       // "SemanticDiag::UnresolvedReference"
    std::string message;
    std::string suggestion;
    std::vector<std::string> notes;
    struct Related {
        uint32_t file = 0;
        Range range;
        std::string message;
    };
    std::vector<Related> related;
};

/// What the compiler reported about one declaration of the syntactic index.
struct DeclFacts {
    /// The types the name had: one, or one per instance of the generic function around it.
    std::vector<uint32_t> types;
    /// Functions: the types of the graphs built from the declaration (instances: several).
    std::vector<uint32_t> signatures;
    /// Imports: the declarations in other files this name stands for.
    std::vector<Location> imported;
    /// Imports and names of the built-in module: what kind of thing was imported.
    bool importedFunction = false;
    bool importedType     = false;
    bool importedMacro    = false;
    std::vector<std::string> importedSignatures;
};

/// What the compiler reported about one reference of the syntactic index.
struct RefFacts {
    bool resolved = false; // the compiler visited this reference
    /// The declarations it resolved to (several: overloads the call has not chosen from).
    std::vector<Location> targets;
    /// For a reference to a value: its type.
    std::vector<uint32_t> types;
    /// For a callee: the type of the function the call resolved to (the instance of a generic
    /// function, the chosen overload), one per instance of the surrounding generic function.
    std::vector<uint32_t> callTypes;
    /// The declaration the call chose, when the name stands for several.
    std::vector<Location> callTargets;
    /// For a name of an operator group (a function implemented outside the language).
    std::vector<std::string> operatorSignatures;
    bool isOperator = false;
    bool macroCall  = false;
};

/// A name that is visible without being declared in a file of the model: a function or type
/// of the built-in module or of an imported module.
struct ExternalName {
    std::string name;
    enum class Kind : uint8_t { Function, Type, Namespace } kind = Kind::Function;
    std::vector<std::string> signatures;
    std::string module;
    bool macro = false;
    std::vector<Location> definitions;
};

struct FileModel {
    std::string path;
    std::string text;
    SyntaxIndex syntax;
    LineIndex lines;
    std::vector<DeclFacts> declFacts; // parallel to syntax.decls
    std::vector<RefFacts> refFacts;   // parallel to syntax.refs
    /// The declaration whose name starts at an offset.
    std::unordered_map<uint32_t, uint32_t> declByNameStart;
    /// The types of expressions, by range; a range may have several (generic instances).
    std::vector<std::pair<Range, uint32_t>> exprTypes;
    /// What each type expression stands for.
    std::vector<std::pair<Range, uint32_t>> typeExprs;
    /// Calls of operators written as operators (`a + b`): the range of the expression the
    /// compiler names as the callee and the type the call resolved to.
    std::vector<std::pair<Range, uint32_t>> operatorCalls;
    bool parsed   = false; // the file has no syntax error
    bool compiled = false; // the module was built without an error
};

class SemanticModel;
using model_ptr_t = std::shared_ptr<const SemanticModel>;

struct AnalysisOptions {
    std::string path; // the file the text belongs to (need not exist)
    std::string text;
    /// When false only the syntactic index is built: nothing is compiled.
    bool compile = true;
    /// When false the compilation runs without a recorder, as `camel check` does: the model
    /// then has diagnostics and the syntactic index only. For measuring what recording costs.
    bool record = true;
    /// A model of an earlier version of the same file. Declarations of the new text that the
    /// compiler says nothing about (the text does not compile) take their types from the
    /// declaration of the same name and place in it.
    model_ptr_t previous;
};

/// A completion candidate.
struct Completion {
    enum class Kind : uint8_t {
        Function,
        Variable,
        Parameter,
        Type,
        TypeParameter,
        Module,
        Field,
        Keyword,
    };
    std::string label;
    Kind kind = Kind::Variable;
    std::string detail; // the type or the signature
    std::string doc;
    int rank = 0; // lower sorts first
};

struct SignatureInfo {
    std::string label;
    std::string doc;
    /// The ranges of the parameters inside `label`, in UTF-8 bytes.
    std::vector<std::pair<uint32_t, uint32_t>> parameters;
};

struct SignatureHelp {
    std::vector<SignatureInfo> signatures;
    uint32_t activeSignature = 0;
    uint32_t activeParameter = 0;
};

struct Hover {
    std::string markdown;
    Range range;
};

struct OutlineItem {
    std::string name;
    std::string detail;
    DeclKind kind = DeclKind::Function;
    Range range;
    Range selection;
    std::vector<OutlineItem> children;
};

struct SemanticToken {
    Range range;
    uint8_t type       = 0; // index into semanticTokenTypes()
    uint32_t modifiers = 0; // bit set over semanticTokenModifiers()
};

const std::vector<std::string> &semanticTokenTypes();
const std::vector<std::string> &semanticTokenModifiers();

struct RenameResult {
    /// Empty when the rename is possible; otherwise why it is refused.
    std::string refusal;
    Range range; // the name under the cursor
    std::string placeholder;
    std::vector<Location> edits; // every range to replace with the new name
};

class SemanticModel {
  public:
    /// Files of the model; file 0 is the analysed text.
    const std::vector<FileModel> &files() const { return files_; }
    const FileModel &main() const { return files_.front(); }
    const std::vector<DiagnosticInfo> &diagnostics() const { return diagnostics_; }
    const TypeInfo &type(uint32_t id) const { return types_[id]; }
    const std::vector<ExternalName> &externals() const { return externals_; }
    /// The exported names of the modules imported as namespaces, by local prefix (`m`, `m::N`).
    const std::map<std::string, std::vector<ExternalName>> &namespaces() const {
        return namespaces_;
    }
    /// The index of the file `path` in files(), if the model has it.
    std::optional<uint32_t> fileOf(const std::string &path) const;

    // ---- Queries; `offset` is a byte offset into the main file ----

    std::optional<Hover> hover(uint32_t offset) const;
    std::vector<Location> definition(uint32_t offset) const;
    /// Every occurrence of what is at `offset`, in all files of the model.
    std::vector<Location> references(uint32_t offset, bool includeDeclaration) const;
    /// Occurrences in the main file, with whether each is a declaration.
    std::vector<std::pair<Range, bool>> highlights(uint32_t offset) const;
    std::vector<OutlineItem> outline() const;
    std::vector<Completion> completions(uint32_t offset) const;
    std::optional<SignatureHelp> signatureHelp(uint32_t offset) const;
    std::vector<SemanticToken> semanticTokens() const;
    /// Whether what is at `offset` can be renamed, and to `newName` when one is given.
    RenameResult rename(uint32_t offset, const std::string &newName) const;
    /// The name of the top-level function around `offset`; empty outside one.
    std::string functionAround(uint32_t offset) const;

    /// A description of the model for tests and debugging: every declaration and reference
    /// of the main file with what was joined to it.
    std::string dump() const;

  private:
    friend class ModelBuilder;
    std::vector<FileModel> files_;
    std::vector<DiagnosticInfo> diagnostics_;
    std::vector<TypeInfo> types_;
    std::vector<ExternalName> externals_;
    std::map<std::string, std::vector<ExternalName>> namespaces_;

    struct Target;
    Target targetAt(uint32_t offset) const;
    std::vector<Location> canonical(const std::vector<Location> &decls) const;
    std::vector<Location> occurrences(const Target &target, bool includeDeclaration) const;
    std::vector<Location> resolvedTargets(uint32_t file, size_t refIndex, size_t segment) const;
    std::string declLabel(uint32_t file, uint32_t decl) const;
    std::string declType(uint32_t file, uint32_t decl) const;
    const ExternalName *external(std::string_view name) const;
    std::optional<uint32_t> typeOfExpression(Range range) const;
    std::optional<uint32_t> typeOfChain(uint32_t end) const;
};

/// Compiles `options.text` and builds its model. Never throws: an error inside the compiler
/// becomes a diagnostic of the model.
model_ptr_t analyze(const AnalysisOptions &options);

/// What `camel --explain` says about the diagnostic with the code `code` ("0x04000002"), as
/// Markdown; empty when the catalog has no such code.
std::string explainDiagnostic(const std::string &code);

} // namespace camel::service::semantic
