// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The order in which a written name is looked for (design principles 10.2, "模块与命名空间").
 *
 * One rule, in one list: the first row that has the name wins. A value name and a type name are to
 * read the same rows -- the value chain does now (`names.cpp`'s `valueNameByRow`) -- each row being
 * asked with the table of its kind and a row a chain has nothing to ask answering "not this row's",
 * so a layer added or moved here is added or moved for every name, and no chain can be changed
 * halfway. `builder_type.cpp`'s `lookupType` is the copy that is not joined yet; it lists the same
 * rows itself, and the step after this one makes it ask this one. It used to be written twice for
 * values as well, `names.cpp`'s chain putting `preludeEntity` last while `builder_type.cpp` carried
 * its own copy, and a rule written twice is a defect that never fails: half a change still
 * compiles, so whoever adds the next layer edits the copy that still runs.
 *
 * The rows are the five layers of that rule and the steps that stand between them, and they are
 * listed once: in `nameRows` below, with what each one means. Read that array for the order; a copy
 * of it here would be the second place the rule is written, which is what this header is for. Two
 * things about the array are not visible from it alone: `EveryImport` is a step and not a layer (an
 * operator takes its overloads from every layer, 10.2, and a name an imported prefix reaches is
 * read with them), and layer 5 is two rows because both chains read it in two steps, in this
 * order -- the imports' unasked names, and then the prelude's exports.
 *
 * The first two rows are answered where the scopes are, by each builder in its own way (the value
 * chain from the graph and template scopes, the type chain from the type scope), so a module-level
 * lookup starts at `rowsFrom(NameRow::ImportedByName)`; those two rows are listed here because the
 * order is one statement, and the reader of either chain has to see where it starts.
 */
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>

namespace camel::compile {

/// One row of the order above. See the file comment: `EveryImport` is a step, not a layer, and
/// layer 5 is read in two rows because both chains read it in two steps, in this order.
enum class NameRow : unsigned char {
    Local,
    Module,
    ImportedByName,
    UsingNamespace,
    EveryImport,
    AmbientImport,
    Prelude,
};

/// A row and what it means, in the order the rows are read.
struct NameRowMeaning {
    NameRow row;
    const char *meaning;
};

/// The order. This array is the only place it is written down.
inline constexpr std::array<NameRowMeaning, 7> nameRows = {{
    {NameRow::Local,
     "the names in force where the name is written: parameters, the lets of the body, and the "
     "names an enclosing function captured"},
    {NameRow::Module,
     "this module's own names, from the innermost namespace out to the top level, and what the C++ "
     "module beside its declaration file exports"},
    {NameRow::ImportedByName,
     "the names an import brings by name, and the names a prefix of an imported module or "
     "namespace reaches"},
    {NameRow::UsingNamespace, "the names a `using namespace` in force brings"},
    {NameRow::EveryImport,
     "the one answer a name takes from every import binding at once: an operator's overloads, and "
     "a name an imported prefix reaches (not a layer)"},
    {NameRow::AmbientImport, "the names this module's imports bring unasked"},
    {NameRow::Prelude, "the names the prelude exports, which come with every module"},
}};

/// The rows from `first` on, in that one order: where a module-level lookup starts.
inline std::span<const NameRowMeaning> rowsFrom(NameRow first) {
    for (size_t i = 0; i < nameRows.size(); ++i) {
        if (nameRows[i].row == first) {
            return std::span<const NameRowMeaning>(nameRows).subspan(i);
        }
    }
    return {};
}

/// What one row says about a written name.
template <typename Result> struct RowAnswer {
    /// Whether this row has the name at all. False means the next row is asked.
    bool holds = false;
    /// What the name stands for, when this row can say. Empty while `holds` is true means the row
    /// has the name and can give nothing for it -- an import of a name the module does not export,
    /// say -- and the caller reports that rather than asking the rows below, which the rule puts
    /// under this one.
    std::optional<Result> found;
    /// Which row answered: set by `firstRowHolding`, meaningless while `holds` is false. A caller
    /// that has to know what KIND of layer answered -- the built-in layer's name is one set made
    /// of two halves, and a name of any other layer's is that layer's alone (design principles
    /// 10.2) -- reads it here rather than asking the rows again in its own order, which would be
    /// the rule written twice.
    NameRow row = NameRow::Local;

    /// Not this row's: the next row is asked.
    static RowAnswer absent() { return {}; }
    /// This row has the name, and this is what it stands for.
    static RowAnswer has(Result value) {
        return RowAnswer{true, std::optional<Result>(std::move(value))};
    }
    /// This row has the name and has nothing to give for it.
    static RowAnswer nothing() { return RowAnswer{true, std::nullopt}; }
};

/// Asks the rows in the order `nameRows` lists them and stops at the first that has the name: the
/// one rule of precedence. Every chain that resolves a written name calls this with a probe that
/// answers a row from that chain's own table, so the order is this array's and nothing else's.
template <typename Result, typename Probe>
RowAnswer<Result> firstRowHolding(std::span<const NameRowMeaning> rows, Probe &&probe) {
    for (const NameRowMeaning &row : rows) {
        RowAnswer<Result> answer = probe(row.row);
        if (answer.holds) {
            answer.row = row.row;
            return answer;
        }
    }
    return RowAnswer<Result>::absent();
}

} // namespace camel::compile