// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Reading the pass declarations of a module out of its `.cml` file.
 *
 * This is the parse layer's half of pass resolution (design principles 9.3): a body-less function
 * declaration conditioned on `is_function` is a pass, and its `with` list is the parameters the
 * pipeline binds. What it produces -- `camel::execute::detail::DeclarationFile` -- is a value, and
 * that is the whole interface: the pipeline's own files (`src/execute/pass.cpp`,
 * `src/execute/pass_declarations.cpp`) never name the parser, and this file never names a pass.
 *
 * The reader is installed when the program loads.
 */

#include "execute/pass_declarations.h"
#include "execute/pass_spec.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/source/manager.h"
#include "camel/parse/ast.h"
#include "camel/parse/parse.h"
#include "camel/utils/path.h"

#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ast = camel::parse::ast;

namespace camel::execute::detail {

namespace {

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
    const ast::node_ptr_t &node, const std::string &module, std::vector<PassDeclaration> &out) {
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
            // A pass is told by its type, `(f: F): F!` under `when is_function::<F>()`, and by
            // nothing else: not by whether the module also implements an operator of the name.
            if (declaration.when.empty() || !hasPassShape(funcType)) {
                return;
            }
            // A pass takes the function it is applied to and nothing else: one ordinary
            // parameter. A body-less declaration with the same condition and more parameters is
            // an operator that wants a function (`export_model(f, example)`), and is not a pass.
            if (funcType->size() > 1 && funcType->at(1)->size() != 1) {
                return;
            }
            if (funcType->size() > 0) {
                try {
                    declaration.params =
                        readPassParams(funcType->at(0), module + "::" + declaration.name);
                } catch (const std::exception &e) {
                    // A body-less declaration whose parameters no pass can take (a rule set, a
                    // cost function) is a pass by its type and is implemented as something else
                    // (an operator of the module): it is listed, and its parameters are the ones
                    // of the operator. A declaration with a body is a pass, and its parameters are
                    // refused.
                    if (func->size() == 1) {
                        declaration.params.clear();
                    } else {
                        throw std::runtime_error(
                            std::format("{}: {}", module + ".cml", e.what()));
                    }
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

/// The first error of a container of parse diagnostics, as the same answer the module loader gives
/// a module that does not compile: "its library 'x' does not compile", which the message that
/// carries it finishes, and the parser's first error as the secondary location. The range is left
/// as the token range the parser gave it until it is resolved here, with the same `RangeConverter`
/// every other reader of a parse uses, so the two paths report the same file, line, column and
/// text.
DeclarationProblem problemOf(
    const camel::core::error::Diagnostics &diags,
    const camel::parse::CamelParser &parser,
    const std::string &module,
    const std::string &path) {
    DeclarationProblem problem{
        std::format("its library '{}' does not compile", module), std::nullopt};
    const auto errors = diags.errors();
    if (errors.empty()) {
        problem.text += std::format(": the declarations in {} do not parse", path);
        return problem;
    }
    camel::core::error::Diagnostic first = *errors.front();
    first.fetchRange(camel::core::error::RangeConverter(parser.tokenPositions()));
    if (const auto *range = std::get_if<camel::core::error::CharRange>(&first.range)) {
        problem.location = camel::core::error::DiagLabel{
            *range, first.name + ": " + first.message, camel::core::error::diagnosticPath(first)};
    } else {
        problem.text += std::format(" {}: {}", first.name, first.message);
    }
    return problem;
}

DeclarationFile readDeclarations(const std::string &module, const std::string &path) {
    DeclarationFile read;
    // The path is Camel text (UTF-8), so it reaches the file system through `pathFromUtf8`:
    // `std::ifstream`'s narrow constructor decodes the bytes in the process code page and would
    // open a file whose name is not the one the search found.
    std::ifstream in(camel::utils::pathFromUtf8(path), std::ios::binary);
    if (!in) {
        read.problem = DeclarationProblem{
            std::format(
                "its library '{}' does not compile: the declarations in {} cannot be read",
                module,
                path),
            std::nullopt};
        return read;
    }
    auto sources = std::make_shared<camel::source::SourceContext>();
    auto diags = std::make_shared<camel::core::error::Diagnostics>("pass", path, sources);
    camel::parse::CamelParser parser(diags);
    parser.setSourceContext(sources);
    if (!parser.parse(in) || diags->hasErrors() || !parser.ast()) {
        read.problem = problemOf(*diags, parser, module, path);
        return read;
    }
    // Module -> Stmts. A declaration the reader refuses (a pass with a body whose parameters no
    // pass can take) is a problem of this file's declarations, not an exception out of it: it
    // leaves this function the same way a file that does not parse does.
    try {
        collectDeclarations(parser.ast()->at(2), module, read.declarations);
    } catch (const std::exception &e) {
        read.declarations.clear();
        read.problem = DeclarationProblem{
            std::format(
                "its library '{}' does not compile: the declarations in {} are refused: {}",
                module,
                path,
                e.what()),
            std::nullopt};
    }
    return read;
}

/// Installs the reader while the program loads, so the pipeline's own files can ask a question
/// about a `.cml` file without owning the parser.
struct ReaderInstalled {
    ReaderInstalled() { setDeclarationFileReader(&readDeclarations); }
};

const ReaderInstalled readerInstalled;

} // namespace

} // namespace camel::execute::detail