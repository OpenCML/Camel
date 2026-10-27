// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What a module name resolves to, and the one order it is looked up in.
 *
 * Design principles 10.2: a module's `import` looks in the importing module's own directory first,
 * then in the module roots (`-I` in the order given, `CAMEL_PACKAGES`), then in the standard
 * library, and the entry program follows the same rule. That order, what counts as a module file,
 * and what a hit means live here, once: a lookup and the report a `ModuleNotFound` carries are the
 * same walk over the same list.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::core::context {

/// True when `name` is a module name a file may be looked up under: a dotted list of non-empty
/// components, `IDENT ('.' IDENT)*`. The grammar's relative spelling (`..lib`, one to three leading
/// dots) is resolved against the importing module's name before any lookup, so a lookup never
/// receives a leading dot; if one reached it, reading the name as a path would turn that dot into a
/// separator and search the filesystem root (`..lib` -> `//lib` -> `/lib`). An import names a
/// module, not a path (design principles 10.2).
bool isModuleName(const std::string &name);

/// What one lookup found.
struct ModuleFile {
    /// The file the name resolved to; empty when it resolved to no file.
    std::string path;
    /// The file is a `.cmo`: a dynamic library, not source. A `.cmo` and a `.cml` of one name in
    /// one directory are one module, and the `.cmo` is what the name stands for (design principles
    /// 6.4): the `.cml` beside it is its declaration module.
    bool isCmo = false;

    bool found() const { return !path.empty(); }
};

/// The lookup of a module name.
///
/// Only `.cml` and `.cmo` name a module file: a same-stem file of any other extension is not a
/// module, so a Markdown document beside `foo.cml` cannot be compiled as `foo`, and which file a
/// name resolves to does not depend on the order the directory lists its files in.
///
/// The directories of one lookup are listed once and the listing is remembered. A listing answers
/// a question about one set of module roots, so `setModuleRoots` drops every listing: a context
/// that re-roots its modules is never answered out of a listing another root list produced.
class ModuleResolver {
  public:
    ModuleResolver(std::string entryDir, std::vector<std::string> moduleRoots);

    /// Replace the module roots (the `-I` directories and the ones the environment adds). The
    /// cached directory listings are dropped, as they were read for the roots in force before.
    void setModuleRoots(std::vector<std::string> roots);

    void invalidateListings();

    const std::string &entryDir() const { return entryDir_; }
    const std::vector<std::string> &moduleRoots() const { return moduleRoots_; }

    /// The directories one lookup tries, in the order it tries them: the importing module's own
    /// directory first (left out when the caller has none), then the module roots. A root that is
    /// written relative is resolved against the entry directory.
    std::vector<std::string> searchRoots(const std::string &importerDir) const;

    /// The file `moduleName` resolves to: the first directory of `searchRoots(importerDir)` that
    /// holds one. A hit is what the name stands for: the caller decides what a file it cannot load
    /// means, but not by looking somewhere else for the name.
    /// An import never resolves to the importing module itself (design principles 10.2): the file
    /// `exceptFile` (the importer's own) is passed over, and the lookup goes on to the next
    /// location of the search order.
    ModuleFile resolve(
        const std::string &moduleName, const std::string &importerDir,
        const std::string &exceptFile = {});

    /// Every directory the lookup tried and why it holds no such module, in the order they were
    /// tried: the reason a `ModuleNotFound` gives. A directory that holds the file contributes
    /// nothing (the caller's own message says what went wrong with it).
    std::vector<std::string>
    searchReport(const std::string &moduleName, const std::string &importerDir);

  private:
    /// The module files of one directory, read once. A stem has at most one `.cml`; several `.cml`
    /// files of one stem are one name written twice and there is nothing to choose between them.
    struct DirectoryListing {
        std::unordered_map<std::string, std::string> source; // stem -> the `.cml` file
        std::unordered_set<std::string> cmo;                 // stems with a `.cmo`
    };

    const DirectoryListing &listingFor(const std::string &directory);
    ModuleFile findUnder(const std::string &baseDir, const std::string &moduleName);
    std::string directoryReason(const std::string &directory, const std::string &stem);
    /// `a.b.c` -> `a/b` and `c`; a name with no component left over is its own stem.
    static void splitLookup(const std::string &moduleName, std::string &dirPart, std::string &stem);

    std::string entryDir_;
    std::vector<std::string> moduleRoots_;
    std::unordered_map<std::string, DirectoryListing> listings_;
};

} // namespace camel::core::context