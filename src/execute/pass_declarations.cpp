// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "pass_declarations.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/operator_table.h"
#include "camel/core/source/manager.h"
#include "camel/utils/path.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <mutex>

namespace camel::execute::detail {

namespace {

/// The files a declaration of `module` could be in, as the file system writes them. A module root
/// is Camel text (UTF-8, design principles 11.1), so it meets the file system through
/// `camel::utils::pathFromUtf8` and not through `std::filesystem::path`'s narrow constructor: that
/// one decodes the bytes in the process code page, and a root outside it then names a file that
/// does not exist -- the search "found" nothing while every candidate the message printed looked
/// right.
std::vector<std::filesystem::path> declarationCandidatesFor(
    const std::string &module, const camel::core::context::context_ptr_t &ctx) {
    std::vector<std::filesystem::path> candidates;
    if (module.empty() || module.find("::") != std::string::npos || !ctx) {
        return candidates;
    }
    const std::filesystem::path file = camel::utils::pathFromUtf8(module + ".cml");
    for (const auto &root : ctx->searchPaths()) {
        if (root.empty()) {
            continue;
        }
        const std::filesystem::path base = camel::utils::pathFromUtf8(root);
        for (const auto &candidate :
             {base / file, base / module / file, base / "stdlib" / file}) {
            candidates.push_back(candidate);
        }
    }
    return candidates;
}

std::string declarationPathFor(
    const std::string &module, const camel::core::context::context_ptr_t &ctx) {
    for (const auto &candidate : declarationCandidatesFor(module, ctx)) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return camel::utils::pathToUtf8(candidate);
        }
    }
    return {};
}


/// The one cache: a file is read once, and both answers it can give (its declarations, the reason
/// it has none) come from that one read. Keyed by path, in the order the search found them.
const DeclarationFile *declarationFileOf(
    const std::string &module, const camel::core::context::context_ptr_t &ctx) {
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<const DeclarationFile>> cache;
    const std::string path = declarationPathFor(module, ctx);
    if (path.empty()) {
        return nullptr;
    }
    std::lock_guard lock(mutex);
    if (const auto it = cache.find(path); it != cache.end()) {
        return it->second.get();
    }
    // The one installed reader, for the one cache below.
    const DeclarationFileReader reader = declarationFileReader();
    DeclarationFile read;
    if (reader == nullptr) {
        // Only possible before the parse layer has loaded, which is to say never in a program:
        // said in the words of every other unreadable declaration file rather than guessed at.
        read.problem = DeclarationProblem{
            std::format(
                "its library '{}' does not compile: no reader of pass declarations is installed",
                module),
            std::nullopt};
    } else {
        read = reader(module, path);
    }
    auto stored = std::make_shared<const DeclarationFile>(std::move(read));
    const DeclarationFile *result = stored.get();
    cache.emplace(path, std::move(stored));
    return result;
}

namespace {

/// The reader the parse layer installed (or null before it has loaded).
DeclarationFileReader &installedReader() {
    static DeclarationFileReader reader = nullptr;
    return reader;
}

} // namespace

const std::vector<PassDeclaration> &declarationsOf(
    const std::string &module, const camel::core::context::context_ptr_t &ctx) {
    static const std::vector<PassDeclaration> none;
    const DeclarationFile *read = declarationFileOf(module, ctx);
    return read == nullptr ? none : read->declarations;
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
    std::vector<std::string> candidates;
    for (const auto &candidate : declarationCandidatesFor(module, ctx)) {
        candidates.push_back(camel::utils::pathToUtf8(candidate));
    }
    return candidates;
}

const std::vector<PassDeclaration> &passDeclarations(
    const std::string &module, const camel::core::context::context_ptr_t &ctx) {
    return declarationsOf(module, ctx);
}

std::optional<DeclarationProblem> declarationProblem(
    const std::string &module, const camel::core::context::context_ptr_t &ctx) {
    const DeclarationFile *read = declarationFileOf(module, ctx);
    if (read == nullptr) {
        return std::nullopt;
    }
    return read->problem;
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

void setDeclarationFileReader(DeclarationFileReader reader) {
    installedReader() = reader;
}

DeclarationFileReader declarationFileReader() {
    return installedReader();
}

} // namespace camel::execute::detail