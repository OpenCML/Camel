// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "pass_declarations.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/operator_table.h"
#include "camel/core/source/manager.h"
#include "camel/parse/ast.h"
#include "camel/parse/parse.h"
#include "pass_spec.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace ast = camel::parse::ast;

namespace camel::execute::detail {

namespace {

std::vector<std::string> declarationCandidatesFor(
    const std::string &module, const camel::core::context::context_ptr_t &ctx) {
    std::vector<std::string> candidates;
    if (module.empty() || module.find("::") != std::string::npos || !ctx) {
        return candidates;
    }
    const std::filesystem::path file = std::filesystem::path(module + ".cml");
    for (const auto &root : ctx->searchPaths()) {
        if (root.empty()) {
            continue;
        }
        const std::filesystem::path base = std::filesystem::path(root);
        for (const auto &candidate :
             {base / file, base / module / file, base / "stdlib" / file}) {
            candidates.push_back(candidate.string());
        }
    }
    return candidates;
}

std::string declarationPathFor(
    const std::string &module, const camel::core::context::context_ptr_t &ctx) {
    for (const auto &candidate : declarationCandidatesFor(module, ctx)) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

/// The `FuncData` child of a function declaration statement.
ast::node_ptr_t funcDataOf(const ast::node_ptr_t &declare) {
    for (size_t i = 0; i < declare->size(); ++i) {
        if (std::dynamic_pointer_cast<ast::FuncDataLoad>(declare->at(i)->load())) {
            return declare->at(i);
        }
    }
    return nullptr;
}

/// Whether the type of a body-less declaration is the shape of a pass, `(f: F): F!` (design
/// principles 9.1): one ordinary parameter written as a name and the failable form of that name as
/// the result. A declaration of another shape that is conditioned on `is_function` declares an
/// operator of a function value, not a pass.
bool hasPassShape(const ast::node_ptr_t &funcType) {
    if (funcType == nullptr || funcType->size() < 3) {
        return false;
    }
    const ast::node_ptr_t &norm = funcType->at(1);
    if (norm->size() != 1 || norm->at(0)->size() == 0 ||
        norm->at(0)->type() != ast::LoadType::NamedPair) {
        return false;
    }
    const ast::node_ptr_t &parameterType = norm->at(0)->at(0);
    const auto parameter = std::dynamic_pointer_cast<ast::RefTypeLoad>(parameterType->load());
    if (parameter == nullptr || funcType->at(2)->size() == 0) {
        return false;
    }
    const ast::node_ptr_t &result = funcType->at(2)->at(0);
    const auto mark = std::dynamic_pointer_cast<ast::NullableTypeLoad>(result->load());
    if (mark == nullptr || !mark->fallible() || result->size() != 1) {
        return false;
    }
    const auto payload = std::dynamic_pointer_cast<ast::RefTypeLoad>(result->at(0)->load());
    return payload != nullptr && payload->ref().toString() == parameter->ref().toString();
}

void collectDeclarations(
    const ast::node_ptr_t &node, const std::string &module,
    std::vector<PassDeclaration> &out) {
    if (node->type() == ast::LoadType::Stmt) {
        const auto stmt = node->loadAs<ast::StmtLoad>();
        if (stmt->stmtType() == ast::StmtType::Func) {
            const auto func = funcDataOf(node);
            if (!func) {
                return;
            }
            const auto data = std::dynamic_pointer_cast<ast::FuncDataLoad>(func->load());
            const auto funcType = func->at(0);
            PassDeclaration declaration;
            declaration.name = data->ref().toString();
            // A declaration inside a namespace is qualified with it; the module prefix is not
            // part of the pass's name.
            if (const std::string prefix = module + "::"; declaration.name.rfind(prefix, 0) == 0) {
                declaration.name = declaration.name.substr(prefix.size());
            }
            declaration.doc  = stmt->doc();
            declaration.when = data->whenText();
            // A pass declares when it applies (`when is_function::<F>()`). An ordinary body-less
            // declaration of the module (`func argsort(...)`) has no such condition and is not a
            // pass: the pipeline resolves it through the module's own exports.
            if (declaration.when.empty() || !hasPassShape(funcType) ||
                camel::core::OperatorTable::instance()
                    .find(module + ":" + declaration.name)
                    .has_value()) {
                return;
            }
            if (funcType->size() > 0) {
                try {
                    declaration.params =
                        readPassParams(funcType->at(0), module + "::" + declaration.name);
                } catch (const std::exception &e) {
                    throw std::runtime_error(
                        std::format("{}: {}", module + ".cml", e.what()));
                }
            }
            out.push_back(std::move(declaration));
            return;
        }
        if (stmt->stmtType() == ast::StmtType::Data) {
            // A top-level value may hold a lambda; it is not a pass declaration.
            return;
        }
    }
    for (size_t i = 0; i < node->size(); ++i) {
        collectDeclarations(node->at(i), module, out);
    }
}

std::vector<PassDeclaration> readDeclarations(
    const std::string &module, const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error(std::format("cannot read the pass declarations in {}", path));
    }
    auto sources = std::make_shared<camel::source::SourceContext>();
    auto diags =
        std::make_shared<camel::core::error::Diagnostics>("pass", path, sources);
    camel::parse::CamelParser parser(diags);
    parser.setSourceContext(sources);
    if (!parser.parse(in) || diags->hasErrors() || !parser.ast()) {
        const auto errors = diags->errors();
        throw std::runtime_error(std::format(
            "{}: {}",
            path,
            errors.empty() ? "the module does not parse" : errors.front()->message));
    }
    std::vector<PassDeclaration> declarations;
    // Module -> Stmts.
    collectDeclarations(parser.ast()->at(2), module, declarations);
    return declarations;
}

const std::vector<PassDeclaration> &declarationsOf(
    const std::string &module, const camel::core::context::context_ptr_t &ctx) {
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<const std::vector<PassDeclaration>>> cache;
    const std::string path = declarationPathFor(module, ctx);
    if (path.empty()) {
        static const std::vector<PassDeclaration> none;
        return none;
    }
    std::lock_guard lock(mutex);
    if (const auto it = cache.find(path); it != cache.end()) {
        return *it->second;
    }
    auto declarations = std::make_shared<const std::vector<PassDeclaration>>(
        readDeclarations(module, path));
    const auto &result = *declarations;
    cache.emplace(path, std::move(declarations));
    return result;
}

bool isIdentifierText(const std::string &text) {
    if (text.empty()) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
}

std::string withoutSpaces(const std::string &text) {
    std::string out;
    for (const char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            out += c;
        }
    }
    return out;
}

} // namespace

std::string moduleDeclarationPath(
    const std::string &module, const camel::core::context::context_ptr_t &ctx) {
    return declarationPathFor(module, ctx);
}

std::vector<std::string> moduleDeclarationCandidates(
    const std::string &module, const camel::core::context::context_ptr_t &ctx) {
    return declarationCandidatesFor(module, ctx);
}

const std::vector<PassDeclaration> &passDeclarations(
    const std::string &module, const camel::core::context::context_ptr_t &ctx) {
    return declarationsOf(module, ctx);
}

const PassDeclaration *findPassDeclaration(
    const std::string &module, const std::string &name,
    const camel::core::context::context_ptr_t &ctx) {
    const auto &declarations = declarationsOf(module, ctx);
    for (const auto &declaration : declarations) {
        if (declaration.name == name) {
            return &declaration;
        }
    }
    return nullptr;
}

bool passDeclarationApplies(const PassDeclaration &declaration, std::string &why) {
    if (declaration.when.empty()) {
        return true;
    }
    // The pipeline applies a pass to the program's root function, so `is_function::<F>()` holds
    // there for the declaration's type parameter. Any other condition is refused rather than
    // treated as true: a declaration whose applicability the pipeline cannot show is not a pass.
    const std::string text = withoutSpaces(declaration.when);
    const std::string prefix = "is_function::<";
    if (text.rfind(prefix, 0) == 0 && text.size() > prefix.size() + 3 &&
        text.compare(text.size() - 3, 3, ">()") == 0) {
        const std::string parameter = text.substr(prefix.size(), text.size() - prefix.size() - 3);
        if (isIdentifierText(parameter)) {
            return true;
        }
    }
    why = std::format(
        "its applicability condition `when {}` is not one the pipeline can show to hold for the "
        "program's root function",
        declaration.when);
    return false;
}

} // namespace camel::execute::detail