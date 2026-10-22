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
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The syntactic half of the semantic model of a source file
 * (docs/technical/32_semantic_query_layer.md).
 *
 * One walk over the parse tree collects what the text alone says: every declaration with the
 * exact range of its name, the lexical scopes, every identifier that refers to something, the
 * member names after a dot, the keys of struct literals and types, and the tokens. Each
 * reference is then resolved by the scoping rules of the language as far as the text allows.
 *
 * It needs no successful compilation: the tree the parser returns for a file with syntax
 * errors is enough (CamelParser::tree), so a file being edited keeps its outline, its local
 * names and its completions. What only the compiler knows (types, the overload a call chose,
 * what an imported name is) is joined to these records by source range in model.h.
 *
 * All ranges are half-open byte offsets into the UTF-8 text of the file.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace camel::parse::syntax {
class Tree;
}

namespace camel::service::semantic {

struct Range {
    uint32_t start = 0;
    uint32_t end   = 0;

    bool empty() const { return end <= start; }
    /// Whether `offset` is inside the range or at its end (a cursor after the last character
    /// of a name is on the name).
    bool touches(uint32_t offset) const { return start <= offset && offset <= end; }
    bool contains(const Range &other) const { return start <= other.start && other.end <= end; }
    bool operator==(const Range &other) const = default;
    uint32_t length() const { return end > start ? end - start : 0; }
};

enum class DeclKind : uint8_t {
    Function,      // func f(...) { ... }
    Parameter,     // (x: T)
    WithParameter, // <x: T>
    Local,         // let x
    Variable,      // var x
    Type,          // type T = ...
    TypeParameter, // type F<T> = ...
    TypeVariable,  // T, declared at the head of a function: `func f::<T>`
    Import,        // import { x } from m / import x from m
    Module,        // import m: the namespace m
    Namespace,     // namespace n { ... }
    Alias,         // use a = b::c
    Binding,       // a name bound by a pattern, a comprehension or a catch clause
};

const char *to_string(DeclKind kind);

struct Decl {
    uint32_t id   = 0;
    DeclKind kind = DeclKind::Local;
    std::string name;
    Range nameRange;           // the identifier
    Range ownerRange;          // the construct the compiler reports the declaration at
    Range fullRange;           // the whole declaration, for outlines
    uint32_t scope       = 0;  // the scope the name is visible in
    uint32_t visibleFrom = 0;  // offset from which it can be referenced; 0: the whole scope
    int32_t container    = -1; // the enclosing function or namespace
    int32_t bodyScope    = -1; // functions, namespaces: the scope they open
    bool exported        = false;
    bool sync            = false; // function modifier `sync`
    bool macro           = false; // function modifier `macro`
    bool generic         = false; // a function whose signature binds a type variable
    bool varParam        = false; // a `var` parameter
    bool lambda          = false; // a parameter of a function literal
    std::string header;           // functions: the declaration without its body; types: all of it
    std::string annotation;       // the type written after the name, if any
    std::string doc;              // the comment lines directly above
    std::string importedFrom;     // Import, Module: the module name as written
    /// A member of a namespace block (and a namespace): its name with the namespaces around
    /// it, `Tools::inc`, as the language server shows it (design principles 10.2); empty for
    /// any other declaration, which is shown by `name`.
    std::string qualified;

    /// The name the declaration is shown by: `qualified` when it has one, `name` otherwise.
    const std::string &shownName() const { return qualified.empty() ? name : qualified; }
};

enum class ScopeKind : uint8_t { Module, Function, Lambda, Block, Namespace, Arm, Type };

struct Scope {
    uint32_t id    = 0;
    int32_t parent = -1;
    ScopeKind kind = ScopeKind::Block;
    Range range;
    int32_t owner = -1; // the function or namespace declaration that opens it
    std::vector<uint32_t> decls;
};

enum class RefKind : uint8_t { Value, Type, Namespace };

/// One segment of `a::b::c`.
struct Segment {
    std::string name;
    Range range;
};

struct Ref {
    RefKind kind = RefKind::Value;
    Range range;               // the whole path
    std::vector<Segment> path; // at least one segment; the last one is the name
    uint32_t scope = 0;
    /// The declaration the first segment names by the scoping rules, or -1. For a path of one
    /// segment that is what the reference names; for `m::f` it is the module or namespace.
    int32_t decl = -1;
    /// For a path through namespaces of this file: the declaration of the last segment.
    int32_t target = -1;
    bool callee    = false; // directly followed by an argument list

    const std::string &name() const { return path.back().name; }
    const Range &nameRange() const { return path.back().range; }
};

/// `.name` or `.0` after an expression.
struct Member {
    std::string name;
    Range nameRange;
    Range receiver; // the expression before the dot
    bool numeric = false;
};

/// The key of a struct literal, a struct type or a named argument: a field name.
struct FieldKey {
    std::string name;
    Range nameRange;
    bool inType = false;
};

struct TokenInfo {
    Range range;
    uint16_t type   = 0; // camel::parse::syntax::TokenKind
    uint8_t channel = 0; // 0 default, 2 command, 3 line comment, 4 block comment
    uint32_t line   = 0; // 0-based line of the first character
};

/// Direct positional arguments of a syntactically named call. Nested expressions
/// keep their whole range; named arguments and arguments after a spread are omitted.
struct CallSite {
    std::vector<Range> positional;
};

struct SyntaxIndex {
    std::vector<Decl> decls;
    std::vector<Scope> scopes;
    std::vector<Ref> refs;       // in source order
    std::vector<Member> members; // in source order
    std::vector<FieldKey> keys;
    std::vector<CallSite> calls;
    std::vector<TokenInfo> tokens; // every token, comments included, without EOF
    /// The expression of every `${...}` of a formatted string, as byte ranges. A completion
    /// inside one is a completion in Camel source, not in a string.
    std::vector<Range> fstringExpressions;
    std::string moduleName; // `module m`
    Range moduleNameRange;
    bool complete = false; // built from a tree without syntax errors

    /// The innermost scope that contains `offset`.
    uint32_t scopeAt(uint32_t offset) const;
    /// The declarations named `name` that a reference at `offset` in `scope` sees: those of
    /// the innermost scope that has one (several: overloads).
    std::vector<uint32_t>
    lookup(uint32_t scope, std::string_view name, uint32_t offset, bool types) const;
    /// Every declaration visible at `offset`, innermost first; a name hidden by an inner one
    /// is left out.
    std::vector<uint32_t> visibleAt(uint32_t offset) const;

    const Decl *declAtName(uint32_t offset) const;
    const Ref *refAt(uint32_t offset) const;
    const Member *memberAt(uint32_t offset) const;
    /// The index of the token at `offset` (a cursor at the end of a token is on it), or -1.
    int32_t tokenAt(uint32_t offset) const;
};

/**
 * Builds the index of one file from its concrete syntax tree: that of a correct text
 * (`complete`) or what the parser recovered from a text with errors. A null tree gives an
 * empty index.
 */
SyntaxIndex buildSyntaxIndex(const camel::parse::syntax::Tree *tree, bool complete);

} // namespace camel::service::semantic
