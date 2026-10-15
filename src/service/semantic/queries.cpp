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
 * The questions a semantic model answers (see model.h), and the line / column arithmetic.
 *
 * Where the compiler said something about a name, that is the answer. Where it did not (the
 * statement is in error, the generic function around it is never called, the text does not
 * parse) the scoping rules applied to the syntactic index give one, and rename, which must
 * not guess, refuses.
 */

#include "camel/utils/windows_parser_guard.h"

#include "camel/service/semantic/model.h"

#include "camel/parse/syntax/token.h"

#include <algorithm>
#include <functional>
#include <set>
#include <sstream>

namespace camel::service::semantic {

// ---------------------------------------------------------------------------------------
// LineIndex
// ---------------------------------------------------------------------------------------

namespace {

// The length in bytes of the UTF-8 sequence that starts with `lead`; 1 for a byte that
// cannot start one, so that malformed text still advances.
uint32_t sequenceLength(unsigned char lead) {
    if (lead < 0x80) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

bool isTypeDeclKind(DeclKind kind) {
    return kind == DeclKind::Type || kind == DeclKind::TypeParameter ||
           kind == DeclKind::TypeVariable;
}

bool isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

} // namespace

LineIndex::LineIndex(std::string_view text) : text_(text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            lineStarts_.push_back(static_cast<uint32_t>(i + 1));
        }
    }
}

uint32_t LineIndex::lineEnd(uint32_t line) const {
    uint32_t end = line + 1 < lineStarts_.size() ? lineStarts_[line + 1] - 1
                                                 : static_cast<uint32_t>(text_.size());
    if (end > lineStarts_[line] && text_[end - 1] == '\r') {
        --end;
    }
    return end;
}

uint32_t LineIndex::offsetOfUtf16(uint32_t line, uint32_t character) const {
    if (line >= lineStarts_.size()) {
        return static_cast<uint32_t>(text_.size());
    }
    const uint32_t end = lineEnd(line);
    uint32_t offset    = lineStarts_[line];
    uint32_t units     = 0;
    while (offset < end && units < character) {
        const uint32_t length =
            std::min(sequenceLength(static_cast<unsigned char>(text_[offset])), end - offset);
        const uint32_t width = length == 4 ? 2 : 1;
        if (units + width > character) {
            break; // inside a surrogate pair: the position before it
        }
        units  += width;
        offset += length;
    }
    return offset;
}

uint32_t LineIndex::offsetOfCodePoint(uint32_t line, uint32_t character) const {
    if (line >= lineStarts_.size()) {
        return static_cast<uint32_t>(text_.size());
    }
    const uint32_t end = lineEnd(line);
    uint32_t offset    = lineStarts_[line];
    for (uint32_t i = 0; i < character && offset < end; ++i) {
        offset += std::min(sequenceLength(static_cast<unsigned char>(text_[offset])), end - offset);
    }
    return offset;
}

LineIndex::Position LineIndex::utf16PositionOf(uint32_t offset) const {
    offset  = std::min<uint32_t>(offset, static_cast<uint32_t>(text_.size()));
    auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
    const uint32_t line = static_cast<uint32_t>(it - lineStarts_.begin()) - 1;
    uint32_t units      = 0;
    uint32_t at         = lineStarts_[line];
    while (at < offset) {
        const uint32_t length = sequenceLength(static_cast<unsigned char>(text_[at]));
        if (at + length > offset) {
            break; // an offset inside a sequence: the position of its start
        }
        units += length == 4 ? 2 : 1;
        at    += length;
    }
    return Position{line, units};
}

// ---------------------------------------------------------------------------------------
// Vocabulary
// ---------------------------------------------------------------------------------------

namespace {

using TK = camel::parse::syntax::TokenKind;

bool isKind(uint16_t type, TK kind) { return type == static_cast<uint16_t>(kind); }

// The keywords of the language, from the lexer's own table so that the list cannot drift
// from it.
const std::vector<std::string> &keywords() {
    static const std::vector<std::string> list = [] {
        std::vector<std::string> out;
        for (const auto &info : camel::parse::syntax::keywordTable()) {
            out.emplace_back(info.text);
        }
        return out;
    }();
    return list;
}

bool isKeywordToken(uint16_t type) {
    return camel::parse::syntax::isKeyword(static_cast<TK>(type));
}

const std::vector<std::string> &builtinTypeNames() {
    static const std::vector<std::string> names = {
        "int", "float", "bool", "string", "void", "i32", "i64", "f32", "f64", "Map", "Set"};
    return names;
}

enum TokenType : uint8_t {
    TNamespace,
    TType,
    TTypeParameter,
    TParameter,
    TVariable,
    TProperty,
    TFunction,
    TMacro,
    TKeyword,
    TOperator,
    TNumber,
    TString,
};
enum TokenModifier : uint32_t {
    MDeclaration    = 1u << 0,
    MReadonly       = 1u << 1,
    MMutable        = 1u << 2,
    MSync           = 1u << 3,
    MDefaultLibrary = 1u << 4,
};

} // namespace

const std::vector<std::string> &semanticTokenTypes() {
    static const std::vector<std::string> types = {
        "namespace",
        "type",
        "typeParameter",
        "parameter",
        "variable",
        "property",
        "function",
        "macro",
        "keyword",
        "operator",
        "number",
        "string"};
    return types;
}

const std::vector<std::string> &semanticTokenModifiers() {
    static const std::vector<std::string> modifiers = {
        "declaration",
        "readonly",
        "mutable",
        "sync",
        "defaultLibrary"};
    return modifiers;
}

// ---------------------------------------------------------------------------------------
// Targets
// ---------------------------------------------------------------------------------------

struct SemanticModel::Target {
    enum class Kind : uint8_t { None, Decls, External, Member } kind = Kind::None;
    Range range; // the name under the cursor
    std::string name;
    /// Kind::Decls: the declarations the name stands for, as file and range of the name.
    std::vector<Location> decls;
    /// Kind::External.
    const ExternalName *external = nullptr;
    std::vector<std::string> operatorSignatures;
    /// The reference under the cursor (index into the main file's refs) and the segment.
    int32_t ref    = -1;
    size_t segment = 0;
    /// The declaration under the cursor (index into the main file's decls).
    int32_t decl = -1;
    const Member *member = nullptr;
};

std::optional<uint32_t> SemanticModel::fileOf(const std::string &path) const {
    for (size_t i = 0; i < files_.size(); ++i) {
        if (files_[i].path == path) {
            return static_cast<uint32_t>(i);
        }
    }
    return std::nullopt;
}

namespace {

const Decl *declOf(const std::vector<FileModel> &files, const Location &loc) {
    if (loc.file >= files.size()) {
        return nullptr;
    }
    const FileModel &file = files[loc.file];
    if (auto it = file.declByNameStart.find(loc.range.start); it != file.declByNameStart.end()) {
        const Decl &decl = file.syntax.decls[it->second];
        if (decl.nameRange == loc.range) {
            return &decl;
        }
    }
    return nullptr;
}

bool sameLocation(const Location &a, const Location &b) {
    return a.file == b.file && a.range == b.range;
}

bool intersects(const std::vector<Location> &a, const std::vector<Location> &b) {
    for (const Location &x : a) {
        for (const Location &y : b) {
            if (sameLocation(x, y)) {
                return true;
            }
        }
    }
    return false;
}

void addLocation(std::vector<Location> &list, const Location &loc) {
    for (const Location &have : list) {
        if (sameLocation(have, loc)) {
            return;
        }
    }
    list.push_back(loc);
}

} // namespace

const ExternalName *SemanticModel::external(std::string_view name) const {
    for (const ExternalName &ext : externals_) {
        if (ext.name == name) {
            return &ext;
        }
    }
    return nullptr;
}

// An imported name stands for what it imports: the declarations in the other file, when the
// compiler told where they are.
std::vector<Location> SemanticModel::canonical(const std::vector<Location> &decls) const {
    std::vector<Location> out;
    for (const Location &loc : decls) {
        const Decl *decl = declOf(files_, loc);
        if (decl != nullptr && decl->kind == DeclKind::Import &&
            !files_[loc.file].declFacts[decl->id].imported.empty()) {
            for (const Location &remote : files_[loc.file].declFacts[decl->id].imported) {
                addLocation(out, remote);
            }
        } else {
            addLocation(out, loc);
        }
    }
    return out;
}

std::vector<Location>
SemanticModel::resolvedTargets(uint32_t file, size_t refIndex, size_t segment) const {
    const FileModel &fm   = files_[file];
    const Ref &ref        = fm.syntax.refs[refIndex];
    const RefFacts &facts = fm.refFacts[refIndex];
    std::vector<Location> out;
    const auto addDecl = [&](uint32_t id) {
        addLocation(out, Location{file, fm.syntax.decls[id].nameRange});
    };
    if (segment + 1 < ref.path.size()) {
        // A namespace segment: only the first one is resolved, by scope.
        if (segment == 0 && ref.decl >= 0) {
            addDecl(static_cast<uint32_t>(ref.decl));
        }
        return canonical(out);
    }
    if (!facts.callTargets.empty()) {
        return canonical(facts.callTargets);
    }
    if (!facts.targets.empty()) {
        return canonical(facts.targets);
    }
    if (ref.path.size() == 1) {
        for (uint32_t id : fm.syntax.lookup(
                 ref.scope,
                 ref.name(),
                 ref.range.start,
                 ref.kind == RefKind::Type)) {
            addDecl(id);
        }
    } else if (ref.target >= 0) {
        addDecl(static_cast<uint32_t>(ref.target));
    }
    return canonical(out);
}

SemanticModel::Target SemanticModel::targetAt(uint32_t offset) const {
    Target target;
    const FileModel &file = main();
    if (const Decl *decl = file.syntax.declAtName(offset)) {
        target.kind  = Target::Kind::Decls;
        target.range = decl->nameRange;
        target.name  = decl->name;
        target.decl  = static_cast<int32_t>(decl->id);
        target.decls = canonical({Location{0, decl->nameRange}});
        return target;
    }
    for (size_t i = 0; i < file.syntax.refs.size(); ++i) {
        const Ref &ref = file.syntax.refs[i];
        if (!ref.range.touches(offset)) {
            continue;
        }
        for (size_t s = 0; s < ref.path.size(); ++s) {
            if (!ref.path[s].range.touches(offset)) {
                continue;
            }
            target.ref     = static_cast<int32_t>(i);
            target.segment = s;
            target.range   = ref.path[s].range;
            target.name    = ref.path[s].name;
            target.decls   = resolvedTargets(0, i, s);
            if (!target.decls.empty()) {
                target.kind = Target::Kind::Decls;
                return target;
            }
            if (s + 1 == ref.path.size()) {
                const RefFacts &facts = file.refFacts[i];
                if (facts.isOperator) {
                    target.kind               = Target::Kind::External;
                    target.operatorSignatures = facts.operatorSignatures;
                    target.external           = ref.path.size() == 1 ? external(ref.name()) : nullptr;
                    return target;
                }
                // A name of the built-in module or of an imported namespace.
                if (ref.path.size() == 1) {
                    target.external = external(ref.name());
                } else {
                    std::string prefix;
                    for (size_t k = 0; k + 1 < ref.path.size(); ++k) {
                        prefix += (k ? "::" : "") + ref.path[k].name;
                    }
                    if (auto ns = namespaces_.find(prefix); ns != namespaces_.end()) {
                        for (const ExternalName &ext : ns->second) {
                            if (ext.name == ref.name()) {
                                target.external = &ext;
                                break;
                            }
                        }
                    }
                }
                // A name in a type is a type: `int` there is not the conversion function.
                if (target.external != nullptr &&
                    (ref.kind == RefKind::Type) !=
                        (target.external->kind == ExternalName::Kind::Type)) {
                    target.external = nullptr;
                }
                if (target.external != nullptr) {
                    target.kind = Target::Kind::External;
                    if (!target.external->definitions.empty()) {
                        target.kind  = Target::Kind::Decls;
                        target.decls = target.external->definitions;
                    }
                    return target;
                }
            }
            // A reference nothing is known about: still a name with a range.
            return target;
        }
    }
    if (const Member *member = file.syntax.memberAt(offset)) {
        target.kind   = Target::Kind::Member;
        target.range  = member->nameRange;
        target.name   = member->name;
        target.member = member;
        return target;
    }
    // An operator that calls an overload the program declared (`a + b` with a
    // `func operator +`): the operator stands for that declaration.
    if (const int32_t token = file.syntax.tokenAt(offset); token >= 0) {
        const Range at = file.syntax.tokens[token].range;
        std::vector<Location> decls;
        for (const auto &[range, location] : file.operatorTargets) {
            if (range == at) {
                decls.push_back(location);
            }
        }
        if (!decls.empty()) {
            target.kind  = Target::Kind::Decls;
            target.range = at;
            target.name  = file.text.substr(at.start, at.length());
            target.decls = canonical(decls);
        }
    }
    return target;
}

std::vector<Location>
SemanticModel::occurrences(const Target &target, bool includeDeclaration) const {
    std::vector<Location> out;
    if (target.kind == Target::Kind::Decls) {
        for (uint32_t f = 0; f < files_.size(); ++f) {
            const FileModel &file = files_[f];
            if (includeDeclaration) {
                // The declarations themselves, and the imports of them in other files.
                for (const Decl &decl : file.syntax.decls) {
                    if (intersects(canonical({Location{f, decl.nameRange}}), target.decls)) {
                        addLocation(out, Location{f, decl.nameRange});
                    }
                }
            }
            for (size_t i = 0; i < file.syntax.refs.size(); ++i) {
                const Ref &ref = file.syntax.refs[i];
                for (size_t s = 0; s < ref.path.size(); ++s) {
                    if (s != 0 && s + 1 != ref.path.size()) {
                        continue;
                    }
                    if (intersects(resolvedTargets(f, i, s), target.decls)) {
                        addLocation(out, Location{f, ref.path[s].range});
                    }
                }
            }
        }
    } else if (target.kind == Target::Kind::External && target.ref >= 0) {
        // A built-in name: every reference of that name that is not a declared one.
        const FileModel &file = main();
        const Ref &self       = file.syntax.refs[target.ref];
        for (size_t i = 0; i < file.syntax.refs.size(); ++i) {
            const Ref &ref = file.syntax.refs[i];
            if (ref.path.size() != self.path.size() || ref.name() != self.name() ||
                ref.kind != self.kind) {
                continue;
            }
            bool samePath = true;
            for (size_t k = 0; k < ref.path.size(); ++k) {
                samePath = samePath && ref.path[k].name == self.path[k].name;
            }
            if (samePath && resolvedTargets(0, i, ref.path.size() - 1).empty()) {
                addLocation(out, Location{0, ref.nameRange()});
            }
        }
    } else if (target.kind == Target::Kind::Member && target.member != nullptr) {
        // The same field of the same type, as far as the receiver types are known.
        const FileModel &file = main();
        const auto receiver   = typeOfExpression(target.member->receiver);
        for (const Member &member : file.syntax.members) {
            if (member.name != target.member->name) {
                continue;
            }
            const auto other = typeOfExpression(member.receiver);
            if (&member == target.member ||
                (receiver && other && types_[*receiver].text == types_[*other].text)) {
                addLocation(out, Location{0, member.nameRange});
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const Location &a, const Location &b) {
        return a.file != b.file ? a.file < b.file : a.range.start < b.range.start;
    });
    return out;
}

// ---------------------------------------------------------------------------------------
// Types of expressions
// ---------------------------------------------------------------------------------------

std::optional<uint32_t> SemanticModel::typeOfExpression(Range range) const {
    const auto &exprs = main().exprTypes;
    std::optional<uint32_t> found;
    for (const auto &[r, type] : exprs) {
        if (r == range) {
            found = type; // the last report for a range is the outermost node built from it
        }
    }
    return found;
}

// The type of the postfix expression that ends at `end`: the smallest expression the compiler
// typed that ends there, else a chain of names `a.b.c` resolved through declared types.
std::optional<uint32_t> SemanticModel::typeOfChain(uint32_t end) const {
    const FileModel &file = main();
    std::optional<std::pair<Range, uint32_t>> best;
    for (const auto &entry : file.exprTypes) {
        if (entry.first.end == end && (!best || entry.first.start >= best->first.start)) {
            best = entry;
        }
    }
    if (best) {
        return best->second;
    }
    // Names and dots, backwards from `end`.
    std::vector<std::string> names;
    uint32_t at = end;
    for (;;) {
        uint32_t start = at;
        while (start > 0 && isWordChar(file.text[start - 1])) {
            --start;
        }
        if (start == at) {
            return std::nullopt;
        }
        names.insert(names.begin(), file.text.substr(start, at - start));
        at = start;
        if (at > 0 && file.text[at - 1] == '.' && (at < 2 || file.text[at - 2] != '.')) {
            --at;
            continue;
        }
        break;
    }
    if (at > 0 && (file.text[at - 1] == ':' || file.text[at - 1] == ')' || file.text[at - 1] == ']')) {
        return std::nullopt; // a path or a call result: not a plain chain of names
    }
    const auto decls = file.syntax.lookup(file.syntax.scopeAt(at), names.front(), at, false);
    if (decls.empty() || file.declFacts[decls.front()].types.empty()) {
        return std::nullopt;
    }
    uint32_t type = file.declFacts[decls.front()].types.front();
    for (size_t i = 1; i < names.size(); ++i) {
        bool found = false;
        for (const auto &[field, fieldType] : types_[type].fields) {
            if (field == names[i]) {
                type  = fieldType;
                found = true;
                break;
            }
        }
        if (!found) {
            return std::nullopt;
        }
    }
    return type;
}

// ---------------------------------------------------------------------------------------
// Labels
// ---------------------------------------------------------------------------------------

std::string SemanticModel::declType(uint32_t file, uint32_t decl) const {
    const DeclFacts &facts = files_[file].declFacts[decl];
    const Decl &d          = files_[file].syntax.decls[decl];
    if (facts.types.size() == 1) {
        return types_[facts.types.front()].text;
    }
    if (!d.annotation.empty()) {
        return d.annotation;
    }
    if (!facts.types.empty()) {
        std::string out;
        for (uint32_t id : facts.types) {
            out += (out.empty() ? "" : " | ") + types_[id].text;
        }
        return out;
    }
    return {};
}

std::string SemanticModel::declLabel(uint32_t file, uint32_t decl) const {
    const Decl &d          = files_[file].syntax.decls[decl];
    const DeclFacts &facts = files_[file].declFacts[decl];
    const std::string type = declType(file, decl);
    const auto typed       = [&](const std::string &prefix) {
        return type.empty() ? prefix + d.name : prefix + d.name + ": " + type;
    };
    switch (d.kind) {
    case DeclKind::Function:
        return d.header;
    case DeclKind::Parameter:
    case DeclKind::WithParameter: {
        // As written (`x: typeas T`), which says more than one instance's type.
        std::string label = d.header.empty() ? typed("") : d.header;
        return label;
    }
    case DeclKind::Local:
        return typed("let ");
    case DeclKind::Variable:
        return typed("var ");
    case DeclKind::Binding:
        return typed("");
    case DeclKind::Type:
        return d.header;
    case DeclKind::TypeParameter:
        return "type parameter " + d.name;
    case DeclKind::TypeVariable:
        return "typeas " + d.name;
    case DeclKind::Import: {
        std::string label = "import { " + d.name + " } from " + d.importedFrom;
        for (const std::string &signature : facts.importedSignatures) {
            label += "\n" + d.name + ": " + signature;
        }
        return label;
    }
    case DeclKind::Module:
        return "import " + d.importedFrom;
    case DeclKind::Namespace:
        return "namespace " + d.name;
    case DeclKind::Alias:
        return "use " + d.name;
    }
    return d.name;
}

// ---------------------------------------------------------------------------------------
// Hover, definition, references
// ---------------------------------------------------------------------------------------

std::optional<Hover> SemanticModel::hover(uint32_t offset) const {
    const FileModel &file = main();
    const Target target   = targetAt(offset);
    std::ostringstream md;
    const auto code = [&](const std::string &text) { md << "```camel\n" << text << "\n```\n"; };

    if (target.kind == Target::Kind::Decls) {
        std::string doc;
        for (const Location &loc : target.decls) {
            const Decl *decl = declOf(files_, loc);
            if (decl == nullptr) {
                continue;
            }
            code(declLabel(loc.file, decl->id));
            const DeclFacts &facts = files_[loc.file].declFacts[decl->id];
            // What the compiler made of the declaration, where the text does not say it.
            if (decl->kind == DeclKind::Function && !facts.signatures.empty() &&
                (decl->generic || decl->annotation.empty())) {
                md << (decl->generic ? "Instances:\n" : "Type:\n");
                std::string list;
                for (uint32_t id : facts.signatures) {
                    list += (list.empty() ? "" : "\n") + types_[id].text;
                }
                code(list);
            }
            if ((decl->kind == DeclKind::Parameter || decl->kind == DeclKind::WithParameter ||
                 decl->kind == DeclKind::Local || decl->kind == DeclKind::Variable) &&
                facts.types.size() > 1) {
                md << "One type per instance of the generic function:\n";
                std::string list;
                for (uint32_t id : facts.types) {
                    list += (list.empty() ? "" : "\n") + decl->name + ": " + types_[id].text;
                }
                code(list);
            }
            if (decl->kind == DeclKind::Parameter && decl->header.empty() == false &&
                facts.types.size() == 1 && types_[facts.types.front()].text != decl->annotation) {
                md << "Type: `" << types_[facts.types.front()].text << "`\n\n";
            }
            if (doc.empty()) {
                doc = decl->doc;
            }
            if (loc.file != 0) {
                md << "Defined in `" << files_[loc.file].path << "`\n\n";
            }
        }
        // At a use: what this use resolved to.
        if (target.ref >= 0 && target.segment + 1 == file.syntax.refs[target.ref].path.size()) {
            const RefFacts &facts = file.refFacts[target.ref];
            if (!facts.callTypes.empty()) {
                md << "This call:\n";
                std::string list;
                for (uint32_t id : facts.callTypes) {
                    list += (list.empty() ? "" : "\n") + types_[id].text;
                }
                code(list);
            } else if (facts.types.size() == 1 && target.decls.size() == 1) {
                const Decl *decl = declOf(files_, target.decls.front());
                if (decl != nullptr &&
                    files_[target.decls.front().file].declFacts[decl->id].types.size() > 1) {
                    md << "Here: `" << types_[facts.types.front()].text << "`\n\n";
                }
            }
        }
        if (target.decl >= 0 && file.syntax.decls[target.decl].kind == DeclKind::Type) {
            // nothing more: the header is the definition
        }
        if (!doc.empty()) {
            md << "\n" << doc << "\n";
        }
        if (md.str().empty()) {
            return std::nullopt;
        }
        return Hover{md.str(), target.range};
    }
    if (target.kind == Target::Kind::External) {
        std::vector<std::string> signatures = target.operatorSignatures;
        if (signatures.empty() && target.external != nullptr) {
            signatures = target.external->signatures;
        }
        std::string list;
        for (const std::string &signature : signatures) {
            list += (list.empty() ? "" : "\n") + target.name + ": " + signature;
        }
        if (list.empty()) {
            list = target.name;
        }
        code(list);
        if (target.ref >= 0 && !file.refFacts[target.ref].callTypes.empty()) {
            md << "This call:\n";
            std::string calls;
            for (uint32_t id : file.refFacts[target.ref].callTypes) {
                calls += (calls.empty() ? "" : "\n") + types_[id].text;
            }
            code(calls);
        }
        if (target.external != nullptr && target.external->kind == ExternalName::Kind::Type) {
            md << "Type";
        } else {
            md << "Function";
        }
        if (target.external != nullptr && !target.external->module.empty()) {
            md << " of module `" << target.external->module << "`\n";
        } else {
            md << " of the built-in module\n";
        }
        return Hover{md.str(), target.range};
    }
    if (target.kind == Target::Kind::Member && target.member != nullptr) {
        const Member &member = *target.member;
        std::optional<uint32_t> type =
            typeOfExpression(Range{member.receiver.start, member.nameRange.end});
        if (!type) {
            if (auto receiver = typeOfChain(member.receiver.end)) {
                for (const auto &[field, fieldType] : types_[*receiver].fields) {
                    if (field == member.name) {
                        type = fieldType;
                    }
                }
            }
        }
        if (!type) {
            return std::nullopt;
        }
        code("(field) " + member.name + ": " + types_[*type].text);
        return Hover{md.str(), target.range};
    }
    // A name of a type that is not declared in a file (`int`), or any typed token.
    if (target.ref >= 0 && file.syntax.refs[target.ref].kind == RefKind::Type) {
        for (const auto &[range, type] : file.typeExprs) {
            if (range == file.syntax.refs[target.ref].range) {
                code("type " + file.syntax.refs[target.ref].name() +
                     (types_[type].text == file.syntax.refs[target.ref].name()
                          ? ""
                          : " = " + types_[type].text));
                return Hover{md.str(), target.range};
            }
        }
    }
    if (target.ref >= 0) {
        const RefFacts &facts = file.refFacts[target.ref];
        if (facts.types.size() == 1) {
            code(target.name + ": " + types_[facts.types.front()].text);
            return Hover{md.str(), target.range};
        }
    }
    const int32_t token = file.syntax.tokenAt(offset);
    if (token >= 0) {
        const TokenInfo &info = file.syntax.tokens[token];
        if (isKind(info.type, TK::Integer) || isKind(info.type, TK::Real) ||
            isKind(info.type, TK::String) || isKind(info.type, TK::MultiStr) ||
            isKind(info.type, TK::FString) || isKind(info.type, TK::KwTrue) ||
            isKind(info.type, TK::KwFalse)) {
            if (auto type = typeOfExpression(info.range)) {
                code(types_[*type].text);
                return Hover{md.str(), info.range};
            }
        }
        // An operator written as an operator: the type the call resolved to.
        for (const auto &[range, type] : file.operatorCalls) {
            if (range == info.range) {
                code("operator " + file.text.substr(range.start, range.length()) + ": " +
                     types_[type].text);
                return Hover{md.str(), info.range};
            }
        }
    }
    return std::nullopt;
}

std::vector<Location> SemanticModel::definition(uint32_t offset) const {
    const Target target = targetAt(offset);
    if (target.kind != Target::Kind::Decls) {
        return {};
    }
    return target.decls;
}

std::vector<Location> SemanticModel::references(uint32_t offset, bool includeDeclaration) const {
    return occurrences(targetAt(offset), includeDeclaration);
}

std::vector<std::pair<Range, bool>> SemanticModel::highlights(uint32_t offset) const {
    std::vector<std::pair<Range, bool>> out;
    for (const Location &loc : occurrences(targetAt(offset), true)) {
        if (loc.file != 0) {
            continue;
        }
        out.emplace_back(loc.range, main().syntax.declAtName(loc.range.start) != nullptr &&
                                        main().syntax.declAtName(loc.range.start)->nameRange ==
                                            loc.range);
    }
    return out;
}

// ---------------------------------------------------------------------------------------
// Outline
// ---------------------------------------------------------------------------------------

std::vector<OutlineItem> SemanticModel::outline() const {
    const FileModel &file = main();
    const auto listed     = [](DeclKind kind) {
        return kind == DeclKind::Function || kind == DeclKind::Type || kind == DeclKind::Local ||
               kind == DeclKind::Variable || kind == DeclKind::Namespace;
    };
    std::function<std::vector<OutlineItem>(int32_t)> childrenOf = [&](int32_t container) {
        std::vector<OutlineItem> items;
        for (const Decl &decl : file.syntax.decls) {
            if (decl.container != container || !listed(decl.kind)) {
                continue;
            }
            OutlineItem item;
            item.name      = decl.name;
            item.kind      = decl.kind;
            item.range     = decl.fullRange;
            item.selection = decl.nameRange;
            if (decl.kind == DeclKind::Function) {
                // The parameters and the return type: the header after the name.
                const size_t at = decl.header.find('(');
                item.detail     = at == std::string::npos ? "" : decl.header.substr(at);
            } else if (decl.kind != DeclKind::Namespace && decl.kind != DeclKind::Type) {
                item.detail = declType(0, decl.id);
            }
            if (decl.kind == DeclKind::Function || decl.kind == DeclKind::Namespace) {
                item.children = childrenOf(static_cast<int32_t>(decl.id));
            }
            items.push_back(std::move(item));
        }
        return items;
    };
    return childrenOf(-1);
}

std::string SemanticModel::functionAround(uint32_t offset) const {
    const FileModel &file = main();
    std::string name;
    for (const Decl &decl : file.syntax.decls) {
        if (decl.kind == DeclKind::Function && decl.fullRange.touches(offset) &&
            (decl.container < 0 ||
             file.syntax.decls[decl.container].kind == DeclKind::Namespace)) {
            name = decl.name;
        }
    }
    return name;
}

// ---------------------------------------------------------------------------------------
// Completion and signature help
// ---------------------------------------------------------------------------------------

namespace {

Completion::Kind completionKind(DeclKind kind) {
    switch (kind) {
    case DeclKind::Function:
        return Completion::Kind::Function;
    case DeclKind::Parameter:
    case DeclKind::WithParameter:
        return Completion::Kind::Parameter;
    case DeclKind::Type:
        return Completion::Kind::Type;
    case DeclKind::TypeParameter:
    case DeclKind::TypeVariable:
        return Completion::Kind::TypeParameter;
    case DeclKind::Module:
    case DeclKind::Namespace:
    case DeclKind::Alias:
        return Completion::Kind::Module;
    default:
        return Completion::Kind::Variable;
    }
}

std::string joinLines(const std::vector<std::string> &lines) {
    std::string out;
    for (const std::string &line : lines) {
        out += (out.empty() ? "" : "\n") + line;
    }
    return out;
}

Completion completionOf(const ExternalName &ext, int rank) {
    Completion item;
    item.label = ext.name;
    item.kind  = ext.kind == ExternalName::Kind::Type
                     ? Completion::Kind::Type
                     : (ext.kind == ExternalName::Kind::Namespace
                            ? Completion::Kind::Module
                            : (ext.kind == ExternalName::Kind::Value ? Completion::Kind::Variable
                                                                     : Completion::Kind::Function));
    item.detail = ext.signatures.empty() ? "" : ext.signatures.front();
    if (ext.signatures.size() > 1) {
        item.detail += "  (+" + std::to_string(ext.signatures.size() - 1) + " overloads)";
        item.doc     = joinLines(ext.signatures);
    }
    item.rank = rank;
    return item;
}

} // namespace

std::vector<Completion> SemanticModel::completions(uint32_t offset) const {
    const FileModel &file = main();
    std::vector<Completion> items;
    offset = std::min<uint32_t>(offset, static_cast<uint32_t>(file.text.size()));

    // Nothing inside a comment or a string.
    for (const TokenInfo &token : file.syntax.tokens) {
        if (token.range.start < offset && offset < token.range.end + (token.channel == 3 ? 1u : 0u) &&
            (token.channel >= 3 || isKind(token.type, TK::String) ||
             isKind(token.type, TK::MultiStr) || isKind(token.type, TK::FString))) {
            return items;
        }
        if (token.range.start > offset) {
            break;
        }
    }

    uint32_t wordStart = offset;
    while (wordStart > 0 && isWordChar(file.text[wordStart - 1])) {
        --wordStart;
    }
    uint32_t before = wordStart;
    while (before > 0 && (file.text[before - 1] == ' ' || file.text[before - 1] == '\t')) {
        --before;
    }

    // After a dot: the fields of the value before it.
    if (before > 0 && file.text[before - 1] == '.' && (before < 2 || file.text[before - 2] != '.')) {
        uint32_t end = before - 1;
        while (end > 0 && (file.text[end - 1] == ' ' || file.text[end - 1] == '\t' ||
                           file.text[end - 1] == '\n' || file.text[end - 1] == '\r')) {
            --end;
        }
        if (auto type = typeOfChain(end)) {
            for (const auto &[field, fieldType] : types_[*type].fields) {
                Completion item;
                item.label  = field;
                item.kind   = Completion::Kind::Field;
                item.detail = types_[fieldType].text;
                items.push_back(std::move(item));
            }
        }
        return items;
    }

    // After `::`: the members of the module or namespace before it.
    if (before >= 2 && file.text[before - 1] == ':' && file.text[before - 2] == ':') {
        std::vector<std::string> path;
        uint32_t at = before - 2;
        for (;;) {
            uint32_t start = at;
            while (start > 0 && isWordChar(file.text[start - 1])) {
                --start;
            }
            if (start == at) {
                break;
            }
            path.insert(path.begin(), file.text.substr(start, at - start));
            at = start;
            if (at >= 2 && file.text[at - 1] == ':' && file.text[at - 2] == ':') {
                at -= 2;
                continue;
            }
            break;
        }
        if (path.empty()) {
            return items;
        }
        std::string prefix;
        for (const std::string &part : path) {
            prefix += (prefix.empty() ? "" : "::") + part;
        }
        if (auto ns = namespaces_.find(prefix); ns != namespaces_.end()) {
            std::set<std::string> seen;
            for (const ExternalName &ext : ns->second) {
                if (seen.insert(ext.name).second) {
                    items.push_back(completionOf(ext, 0));
                }
            }
        }
        // A namespace declared in this file.
        auto decls = file.syntax.lookup(file.syntax.scopeAt(offset), path.front(), offset, false);
        int32_t current = decls.empty() ? -1 : static_cast<int32_t>(decls.front());
        for (size_t i = 1; i < path.size() && current >= 0; ++i) {
            const Decl &decl = file.syntax.decls[current];
            int32_t next     = -1;
            if (decl.bodyScope >= 0) {
                for (uint32_t id : file.syntax.scopes[decl.bodyScope].decls) {
                    if (file.syntax.decls[id].name == path[i]) {
                        next = static_cast<int32_t>(id);
                    }
                }
            }
            current = next;
        }
        if (current >= 0 && file.syntax.decls[current].kind == DeclKind::Namespace &&
            file.syntax.decls[current].bodyScope >= 0) {
            for (uint32_t id : file.syntax.scopes[file.syntax.decls[current].bodyScope].decls) {
                const Decl &decl = file.syntax.decls[id];
                Completion item;
                item.label  = decl.name;
                item.kind   = completionKind(decl.kind);
                item.detail = decl.kind == DeclKind::Function ? decl.header : declType(0, id);
                item.doc    = decl.doc;
                items.push_back(std::move(item));
            }
        }
        return items;
    }

    // Names in scope, innermost first.
    std::set<std::string> seen;
    for (uint32_t id : file.syntax.visibleAt(offset)) {
        const Decl &decl = file.syntax.decls[id];
        // The name being declared at the cursor is not a candidate for itself.
        if (decl.nameRange.touches(offset)) {
            continue;
        }
        Completion item;
        item.label = decl.name;
        item.kind  = completionKind(decl.kind);
        if (decl.kind == DeclKind::Import) {
            const DeclFacts &facts = file.declFacts[id];
            item.kind   = facts.importedType && !facts.importedFunction ? Completion::Kind::Type
                                                                        : Completion::Kind::Function;
            item.detail = facts.importedSignatures.empty() ? "from " + decl.importedFrom
                                                           : facts.importedSignatures.front();
        } else if (decl.kind == DeclKind::Function) {
            item.detail = decl.header;
        } else if (decl.kind == DeclKind::Type) {
            item.detail = decl.header;
        } else {
            item.detail = declType(0, id);
        }
        item.doc  = decl.doc;
        item.rank = decl.scope == 0 ? 1 : 0;
        if (!seen.insert(decl.name).second) {
            continue; // overloads: one entry
        }
        items.push_back(std::move(item));
    }
    for (const ExternalName &ext : externals_) {
        // Operators are written as operators: `__add__` is not something to type.
        if (ext.name.starts_with("__") || !seen.insert(ext.name).second) {
            continue;
        }
        items.push_back(completionOf(ext, 2));
    }
    for (const std::string &name : builtinTypeNames()) {
        if (seen.insert(name).second) {
            Completion item;
            item.label  = name;
            item.kind   = Completion::Kind::Type;
            item.detail = "built-in type";
            item.rank   = 3;
            items.push_back(std::move(item));
        }
    }
    for (const std::string &keyword : keywords()) {
        if (seen.insert(keyword).second) {
            Completion item;
            item.label = keyword;
            item.kind  = Completion::Kind::Keyword;
            item.rank  = 4;
            items.push_back(std::move(item));
        }
    }
    return items;
}

namespace {

// The parameters of a signature as the compiler prints a function type: the parts between
// the top-level commas of each top-level bracket group before `=>`.
std::vector<std::pair<uint32_t, uint32_t>> parametersIn(const std::string &label) {
    std::vector<std::pair<uint32_t, uint32_t>> out;
    const size_t arrow = label.rfind("=>");
    int depth          = 0;
    size_t start       = std::string::npos;
    const auto flush   = [&](size_t end) {
        if (start == std::string::npos) {
            return;
        }
        size_t a = start, b = end;
        while (a < b && label[a] == ' ') {
            ++a;
        }
        while (b > a && label[b - 1] == ' ') {
            --b;
        }
        if (b > a) {
            out.emplace_back(static_cast<uint32_t>(a), static_cast<uint32_t>(b));
        }
        start = std::string::npos;
    };
    for (size_t i = 0; i < label.size() && (arrow == std::string::npos || i < arrow); ++i) {
        const char c = label[i];
        if (c == '(' || c == '<' || c == '[' || c == '{') {
            if (depth == 0 && (c == '(' || c == '<')) {
                start = i + 1;
            }
            ++depth;
        } else if (c == ')' || c == '>' || c == ']' || c == '}') {
            --depth;
            if (depth == 0) {
                flush(i);
            }
        } else if (c == ',' && depth == 1) {
            flush(i);
            start = i + 1;
        }
    }
    return out;
}

} // namespace

std::optional<SignatureHelp> SemanticModel::signatureHelp(uint32_t offset) const {
    const FileModel &file = main();
    const auto &tokens    = file.syntax.tokens;
    const auto textOf     = [&](const TokenInfo &token) {
        return std::string_view(file.text).substr(token.range.start, token.range.length());
    };
    // The tokens before the cursor, backwards: the innermost open `(` that follows a name.
    int32_t i = static_cast<int32_t>(tokens.size()) - 1;
    while (i >= 0 && (tokens[i].range.start >= offset || tokens[i].channel != 0)) {
        --i;
    }
    int depth       = 0;
    uint32_t commas = 0;
    int32_t open    = -1;
    for (; i >= 0; --i) {
        if (tokens[i].channel != 0) {
            continue;
        }
        const std::string_view text = textOf(tokens[i]);
        if (text == ")" || text == "]" || text == "}") {
            ++depth;
        } else if (text == "(" || text == "[" || text == "{") {
            if (depth > 0) {
                --depth;
                continue;
            }
            if (text == "{") {
                return std::nullopt; // a block: its statements are not arguments
            }
            int32_t prev = i - 1;
            while (prev >= 0 && tokens[prev].channel != 0) {
                --prev;
            }
            if (text == "(" && prev >= 0 && isKind(tokens[prev].type, TK::Identifier) &&
                tokens[prev].range.end == tokens[i].range.start) {
                open = i;
                break;
            }
            commas = 0; // inside a tuple, an index or an array of an outer call
        } else if (text == "," && depth == 0) {
            ++commas;
        }
    }
    if (open < 0) {
        return std::nullopt;
    }
    // The callee: `a::b::f` before the parenthesis.
    std::vector<std::string> path;
    int32_t at = open - 1;
    for (;;) {
        path.insert(path.begin(), std::string(textOf(tokens[at])));
        if (at >= 2 && textOf(tokens[at - 1]) == "::" &&
            isKind(tokens[at - 2].type, TK::Identifier)) {
            at -= 2;
            continue;
        }
        break;
    }
    // A declaration header `func f(` is not a call.
    if (at >= 1 && isKind(tokens[at - 1].type, TK::KwFunc)) {
        return std::nullopt;
    }
    SignatureHelp help;
    help.activeParameter = commas;
    const uint32_t callAt = tokens[at].range.start;
    const auto addExternal = [&](const ExternalName &ext) {
        for (const std::string &signature : ext.signatures) {
            SignatureInfo info;
            info.label      = ext.name + ": " + signature;
            info.parameters = parametersIn(info.label);
            help.signatures.push_back(std::move(info));
        }
    };
    if (path.size() == 1) {
        const auto decls = file.syntax.lookup(file.syntax.scopeAt(callAt), path.front(), callAt, false);
        for (uint32_t id : decls) {
            const Decl &decl = file.syntax.decls[id];
            SignatureInfo info;
            if (decl.kind == DeclKind::Function) {
                info.label = decl.header;
                info.doc   = decl.doc;
                // The parameters as written, found in order in the header.
                size_t from = 0;
                if (decl.bodyScope >= 0) {
                    std::vector<const Decl *> with, norm;
                    for (uint32_t p : file.syntax.scopes[decl.bodyScope].decls) {
                        const Decl &param = file.syntax.decls[p];
                        if (param.kind == DeclKind::WithParameter) {
                            with.push_back(&param);
                        } else if (param.kind == DeclKind::Parameter) {
                            norm.push_back(&param);
                        }
                    }
                    for (const Decl *param : norm) {
                        // Positional parameters come after the name.
                        const size_t nameAt = info.label.find(decl.name + "(");
                        from                = std::max(from, nameAt == std::string::npos ? 0 : nameAt);
                        const size_t found  = info.label.find(param->header, from);
                        if (found == std::string::npos) {
                            continue;
                        }
                        info.parameters.emplace_back(
                            static_cast<uint32_t>(found),
                            static_cast<uint32_t>(found + param->header.size()));
                        from = found + param->header.size();
                    }
                }
            } else if (decl.kind == DeclKind::Import) {
                for (const std::string &signature : file.declFacts[id].importedSignatures) {
                    SignatureInfo imported;
                    imported.label      = decl.name + ": " + signature;
                    imported.parameters = parametersIn(imported.label);
                    help.signatures.push_back(std::move(imported));
                }
                continue;
            } else {
                const std::string type = declType(0, id);
                if (type.empty()) {
                    continue;
                }
                info.label      = decl.name + ": " + type;
                info.parameters = parametersIn(info.label);
            }
            help.signatures.push_back(std::move(info));
        }
        if (help.signatures.empty()) {
            if (const ExternalName *ext = external(path.front())) {
                addExternal(*ext);
            }
        }
    } else {
        std::string prefix;
        for (size_t k = 0; k + 1 < path.size(); ++k) {
            prefix += (k ? "::" : "") + path[k];
        }
        if (auto ns = namespaces_.find(prefix); ns != namespaces_.end()) {
            for (const ExternalName &ext : ns->second) {
                if (ext.name == path.back()) {
                    addExternal(ext);
                }
            }
        }
    }
    if (help.signatures.empty()) {
        return std::nullopt;
    }
    for (size_t s = 0; s < help.signatures.size(); ++s) {
        if (help.signatures[s].parameters.size() > commas) {
            help.activeSignature = static_cast<uint32_t>(s);
            break;
        }
    }
    return help;
}

// ---------------------------------------------------------------------------------------
// Semantic tokens
// ---------------------------------------------------------------------------------------

std::vector<SemanticToken> SemanticModel::semanticTokens() const {
    const FileModel &file = main();
    // What each identifier is, by the start of its range.
    std::map<uint32_t, SemanticToken> names;
    const auto put = [&](Range range, uint8_t type, uint32_t modifiers) {
        names[range.start] = SemanticToken{range, type, modifiers};
    };
    const auto classify = [&](uint32_t f, const Decl &decl, bool declaration) {
        uint32_t mods = declaration ? MDeclaration : 0;
        switch (decl.kind) {
        case DeclKind::Function:
            return std::pair<uint8_t, uint32_t>(
                decl.macro ? TMacro : TFunction,
                mods | (decl.sync ? MSync : 0));
        case DeclKind::Parameter:
        case DeclKind::WithParameter:
            return std::pair<uint8_t, uint32_t>(
                TParameter,
                mods | (decl.varParam ? MMutable : MReadonly));
        case DeclKind::Local:
        case DeclKind::Binding:
            return std::pair<uint8_t, uint32_t>(TVariable, mods | MReadonly);
        case DeclKind::Variable:
            return std::pair<uint8_t, uint32_t>(TVariable, mods | MMutable);
        case DeclKind::Type:
            return std::pair<uint8_t, uint32_t>(TType, mods);
        case DeclKind::TypeParameter:
        case DeclKind::TypeVariable:
            return std::pair<uint8_t, uint32_t>(TTypeParameter, mods);
        case DeclKind::Import: {
            const DeclFacts &facts = files_[f].declFacts[decl.id];
            if (facts.importedType && !facts.importedFunction) {
                return std::pair<uint8_t, uint32_t>(TType, mods);
            }
            return std::pair<uint8_t, uint32_t>(facts.importedMacro ? TMacro : TFunction, mods);
        }
        case DeclKind::Module:
        case DeclKind::Namespace:
        case DeclKind::Alias:
            return std::pair<uint8_t, uint32_t>(TNamespace, mods);
        }
        return std::pair<uint8_t, uint32_t>(TVariable, mods);
    };
    for (const Decl &decl : file.syntax.decls) {
        const auto [type, mods] = classify(0, decl, true);
        put(decl.nameRange, type, mods);
    }
    for (const FieldKey &key : file.syntax.keys) {
        put(key.nameRange, TProperty, 0);
    }
    for (const Member &member : file.syntax.members) {
        if (!member.numeric) {
            put(member.nameRange, TProperty, 0);
        }
    }
    for (size_t i = 0; i < file.syntax.refs.size(); ++i) {
        const Ref &ref = file.syntax.refs[i];
        for (size_t s = 0; s + 1 < ref.path.size(); ++s) {
            put(ref.path[s].range, TNamespace, 0);
        }
        const RefFacts &facts = file.refFacts[i];
        // What the name stands for here: the declaration as written in this file (an imported
        // name keeps the kind of its import), else what the compiler found.
        std::vector<Location> targets;
        if (ref.path.size() == 1 && ref.decl >= 0 && facts.targets.empty() &&
            facts.callTargets.empty()) {
            targets.push_back(Location{0, file.syntax.decls[ref.decl].nameRange});
        } else {
            targets = resolvedTargets(0, i, ref.path.size() - 1);
        }
        if (!targets.empty()) {
            if (const Decl *decl = declOf(files_, targets.front())) {
                auto [type, mods] = classify(targets.front().file, *decl, false);
                if (facts.macroCall && type == TFunction) {
                    type = TMacro;
                }
                put(ref.nameRange(), type, mods);
                continue;
            }
        }
        if (facts.isOperator) {
            put(ref.nameRange(), facts.macroCall ? TMacro : TFunction, MDefaultLibrary);
            continue;
        }
        if (ref.kind == RefKind::Type) {
            const bool builtin =
                ref.path.size() == 1 &&
                std::find(builtinTypeNames().begin(), builtinTypeNames().end(), ref.name()) !=
                    builtinTypeNames().end();
            put(ref.nameRange(), TType, builtin ? MDefaultLibrary : 0);
            continue;
        }
        const ExternalName *ext = nullptr;
        if (ref.path.size() == 1) {
            ext = external(ref.name());
        } else {
            std::string prefix;
            for (size_t k = 0; k + 1 < ref.path.size(); ++k) {
                prefix += (k ? "::" : "") + ref.path[k].name;
            }
            if (auto ns = namespaces_.find(prefix); ns != namespaces_.end()) {
                for (const ExternalName &candidate : ns->second) {
                    if (candidate.name == ref.name()) {
                        ext = &candidate;
                        break;
                    }
                }
            }
        }
        if (ext != nullptr) {
            if (ext->kind == ExternalName::Kind::Value) {
                put(ref.nameRange(), TVariable, MReadonly);
            } else {
                const uint8_t type = ext->kind == ExternalName::Kind::Type
                                         ? TType
                                         : (ext->kind == ExternalName::Kind::Namespace
                                                ? TNamespace
                                                : (ext->macro || facts.macroCall ? TMacro
                                                                                 : TFunction));
                put(ref.nameRange(), type, ext->module.empty() ? MDefaultLibrary : 0);
            }
        }
        // Otherwise unknown: no token, the editor's grammar colours it.
    }

    std::vector<SemanticToken> out;
    for (const TokenInfo &token : file.syntax.tokens) {
        if (token.channel != 0 || token.range.empty()) {
            continue;
        }
        const std::string_view text =
            std::string_view(file.text).substr(token.range.start, token.range.length());
        if (text.find('\n') != std::string_view::npos) {
            continue; // a token over several lines cannot be expressed; the grammar has it
        }
        if (isKind(token.type, TK::Identifier)) {
            if (auto it = names.find(token.range.start); it != names.end()) {
                out.push_back(it->second);
            }
        } else if (isKeywordToken(token.type)) {
            out.push_back({token.range, TKeyword, 0});
        } else if (isKind(token.type, TK::Integer) || isKind(token.type, TK::Real)) {
            // `.0` after a dot is a tuple index, lexed as an integer.
            out.push_back({token.range, TNumber, 0});
        } else if (isKind(token.type, TK::String) || isKind(token.type, TK::FString)) {
            out.push_back({token.range, TString, 0});
        } else if (
            isKind(token.type, TK::DotDot) ||
            (camel::parse::syntax::isPunctuation(static_cast<TK>(token.type)) &&
             text.find_first_of("+-*/%^@&|!=<>~?") != std::string_view::npos && text != "=>")) {
            out.push_back({token.range, TOperator, 0});
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------
// Rename
// ---------------------------------------------------------------------------------------

RenameResult SemanticModel::rename(uint32_t offset, const std::string &newName) const {
    RenameResult result;
    const FileModel &file = main();
    const Target target   = targetAt(offset);
    result.range          = target.range;
    result.placeholder    = target.name;
    const auto refuse     = [&](std::string why) {
        result.refusal = std::move(why);
        return result;
    };
    if (target.kind == Target::Kind::External) {
        return refuse("'" + target.name + "' is not declared in a source file of this program");
    }
    if (target.kind == Target::Kind::Member) {
        return refuse(
            "a field cannot be renamed: the uses of a field of a struct type are not tracked");
    }
    if (target.kind != Target::Kind::Decls || target.decls.empty()) {
        return refuse("there is nothing here that can be renamed");
    }
    if (!file.parsed) {
        return refuse("the file has syntax errors: its references cannot be determined");
    }
    if (!file.compiled) {
        return refuse(
            "the file has errors: the compiler has not resolved every reference, so a rename "
            "could miss some");
    }
    // The declarations to rename: every overload of the name in its scope.
    std::vector<Location> decls;
    for (const Location &loc : target.decls) {
        const Decl *decl = declOf(files_, loc);
        if (decl == nullptr) {
            return refuse("the declaration of '" + target.name + "' was not found");
        }
        if (loc.file != 0) {
            return refuse(
                "'" + target.name + "' is declared in another file (" + files_[loc.file].path +
                "); rename it there");
        }
        switch (decl->kind) {
        case DeclKind::Import:
        case DeclKind::Module:
            return refuse("'" + target.name + "' is the name of something in another module");
        case DeclKind::Namespace:
        case DeclKind::Alias:
            return refuse("renaming a namespace is not supported");
        default:
            break;
        }
        for (uint32_t id : file.syntax.scopes[decl->scope].decls) {
            const Decl &other = file.syntax.decls[id];
            if (other.name == decl->name && (other.kind == DeclKind::Function) ==
                                                (decl->kind == DeclKind::Function)) {
                if (other.exported) {
                    return refuse(
                        "'" + target.name +
                        "' is exported: modules that import it are not known here and would break");
                }
                addLocation(decls, Location{0, other.nameRange});
            }
        }
    }
    const Decl &first = *declOf(files_, decls.front());
    if (first.scope == 0 && first.name == "main") {
        return refuse("'main' is the entry of the program");
    }

    // Every identifier spelt like the name must be accounted for: a reference the compiler
    // resolved to these declarations, a reference it resolved elsewhere, or not a reference
    // at all (a field, a key). One the compiler did not visit could be either: refuse.
    const bool isType = isTypeDeclKind(first.kind);
    std::vector<Location> edits = decls;
    size_t unconfirmed          = 0;
    for (size_t i = 0; i < file.syntax.refs.size(); ++i) {
        const Ref &ref = file.syntax.refs[i];
        for (size_t s = 0; s < ref.path.size(); ++s) {
            if (ref.path[s].name != first.name) {
                continue;
            }
            const bool last = s + 1 == ref.path.size();
            // By scope:
            bool lexical = false;
            if (s == 0 && ref.path.size() == 1) {
                for (uint32_t id : file.syntax.lookup(
                         ref.scope,
                         ref.name(),
                         ref.range.start,
                         ref.kind == RefKind::Type)) {
                    lexical = lexical ||
                              intersects({Location{0, file.syntax.decls[id].nameRange}}, decls);
                }
            } else if (last && ref.target >= 0) {
                lexical = intersects({Location{0, file.syntax.decls[ref.target].nameRange}}, decls);
            }
            // By the compiler:
            const RefFacts &facts = file.refFacts[i];
            const bool visited    = last && (facts.resolved || !facts.callTargets.empty());
            bool compiled         = false;
            if (visited) {
                compiled = intersects(canonical(facts.targets), decls) ||
                           intersects(canonical(facts.callTargets), decls);
            }
            if (isType || ref.kind != RefKind::Value || !last) {
                // Types and namespaces are resolved by scope alone, also by the compiler.
                if (lexical) {
                    addLocation(edits, Location{0, ref.path[s].range});
                }
                continue;
            }
            if (visited) {
                if (compiled != lexical) {
                    return refuse(
                        "the compiler and the scoping rules disagree about a use of '" +
                        first.name + "'; this is a defect of the language server");
                }
                if (compiled) {
                    addLocation(edits, Location{0, ref.path[s].range});
                }
            } else if (lexical) {
                ++unconfirmed;
            }
        }
    }
    if (unconfirmed > 0) {
        return refuse(
            std::to_string(unconfirmed) + " use(s) of '" + first.name +
            "' are in code the compiler did not build (a generic function that is never "
            "called): they cannot be confirmed");
    }
    // Other files of the program that use the name.
    for (uint32_t f = 1; f < files_.size(); ++f) {
        for (size_t i = 0; i < files_[f].syntax.refs.size(); ++i) {
            if (intersects(resolvedTargets(f, i, files_[f].syntax.refs[i].path.size() - 1), decls)) {
                return refuse("'" + first.name + "' is used in another file: " + files_[f].path);
            }
        }
    }

    if (!newName.empty()) {
        const bool identifier =
            !(newName[0] >= '0' && newName[0] <= '9') &&
            std::all_of(newName.begin(), newName.end(), isWordChar);
        if (!identifier) {
            return refuse("'" + newName + "' is not an identifier");
        }
        if (std::find(keywords().begin(), keywords().end(), newName) != keywords().end()) {
            return refuse("'" + newName + "' is a keyword");
        }
        if (newName != first.name) {
            // The new name must not meet a name already in use where the old one is visible:
            // a declaration in the same scope would clash, one inside would capture uses,
            // a use inside would be captured.
            const Range scope = file.syntax.scopes[first.scope].range;
            for (const Decl &decl : file.syntax.decls) {
                if (decl.name == newName && scope.contains(decl.nameRange)) {
                    return refuse(
                        "'" + newName + "' is already declared where '" + first.name +
                        "' is visible");
                }
            }
            for (const Ref &ref : file.syntax.refs) {
                if (ref.path.front().name == newName && scope.contains(ref.range)) {
                    return refuse(
                        "'" + newName + "' is already used where '" + first.name +
                        "' is visible: the rename would change what it refers to");
                }
            }
        }
    }
    std::sort(edits.begin(), edits.end(), [](const Location &a, const Location &b) {
        return a.range.start < b.range.start;
    });
    result.edits = std::move(edits);
    return result;
}

// ---------------------------------------------------------------------------------------
// Dump
// ---------------------------------------------------------------------------------------

std::string SemanticModel::dump() const {
    std::ostringstream os;
    const FileModel &file = main();
    const auto at         = [&](uint32_t f, Range r) {
        const auto p = files_[f].lines.utf16PositionOf(r.start);
        return (f == 0 ? std::string() : files_[f].path + ":") + std::to_string(p.line + 1) + ":" +
               std::to_string(p.character + 1);
    };
    os << "file " << file.path << " parsed=" << file.parsed << " compiled=" << file.compiled
       << " files=" << files_.size() << "\n";
    os << "decls:\n";
    for (const Decl &decl : file.syntax.decls) {
        const DeclFacts &facts = file.declFacts[decl.id];
        os << "  " << at(0, decl.nameRange) << " " << to_string(decl.kind) << " " << decl.name;
        const std::string type = declType(0, decl.id);
        if (!type.empty()) {
            os << " : " << type;
        }
        for (uint32_t id : facts.signatures) {
            os << " sig[" << types_[id].text << "]";
        }
        for (const std::string &signature : facts.importedSignatures) {
            os << " imp[" << signature << "]";
        }
        for (const Location &loc : facts.imported) {
            os << " -> " << at(loc.file, loc.range);
        }
        os << "\n";
    }
    os << "refs:\n";
    for (size_t i = 0; i < file.syntax.refs.size(); ++i) {
        const Ref &ref        = file.syntax.refs[i];
        const RefFacts &facts = file.refFacts[i];
        os << "  " << at(0, ref.range) << " " << file.text.substr(ref.range.start, ref.range.length())
           << (ref.kind == RefKind::Type ? " (type)" : "") << (ref.callee ? " (callee)" : "");
        os << (facts.resolved ? " compiler" : " scope");
        for (const Location &loc : resolvedTargets(0, i, ref.path.size() - 1)) {
            os << " -> " << at(loc.file, loc.range);
        }
        for (uint32_t id : facts.types) {
            os << " : " << types_[id].text;
        }
        for (uint32_t id : facts.callTypes) {
            os << " call[" << types_[id].text << "]";
        }
        if (facts.isOperator) {
            os << " operator(" << facts.operatorSignatures.size() << ")";
        }
        os << "\n";
    }
    os << "members:\n";
    for (const Member &member : file.syntax.members) {
        os << "  " << at(0, member.nameRange) << " ." << member.name;
        if (auto type = typeOfExpression(Range{member.receiver.start, member.nameRange.end})) {
            os << " : " << types_[*type].text;
        }
        os << "\n";
    }
    os << "externals: " << externals_.size() << ", namespaces: " << namespaces_.size() << "\n";
    return os.str();
}

} // namespace camel::service::semantic
