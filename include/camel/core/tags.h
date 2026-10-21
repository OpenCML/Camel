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
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Tags: compile-time facts attached to declarations and to nodes (design principles 6.2).
 *
 * A tag is a name in the same namespace as functions and types. It is declared once, by a
 * Camel module (`tag vjp<rule: func> on func`) or by a C++ module (declare, below), and it
 * says where it may be written (`Func`, `Type`, `Field`, `Nodes`) and which passes read it.
 * Writing `#tag` at a position the tag does not list is a compile error (the graph builder
 * reports it at the tag).
 *
 * The language gives a tag no meaning: the library that declares it, and the pass that reads
 * it, define it. The core keeps only where the facts are:
 *
 *   - On nodes: an ordinary node attribute (camel/core/node_attrs.h). A tag declared `on
 *     nodes` owns one attribute key; the facts of the regions a node is written in accumulate
 *     by the key's combine rule, and a same-name tag with a parameter written in an inner
 *     region wins (attr::rules::inner).
 *   - On declarations: an attribute set kept by the module that declares the function, type
 *     or field (module::Module::declarationTags). A C++ module reads it with
 *     ofDeclaration/ofField; reading it from Camel code (`match type`, `fields`) comes with
 *     the first stage of generics, as the handover says.
 *
 * A parameterized tag's value is its arguments: one argument is the value itself (an integer,
 * a symbol, a graph reference); several make a tuple. A tag without parameters has the unit
 * value. The value of a tag a C++ module declares may instead be built from its arguments by
 * the callback that declaration gives (declareNodeTag).
 *
 * Thread-safety: declaration takes a lock and may be called from any thread (modules load in
 * parallel); reading takes a shared lock. Ids are process-local and never reused, and the
 * Decl of an id is not moved after it is published, so get() may be called while another
 * thread declares.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "camel/common/ref.h"
#include "camel/core/data/base.h"
#include "camel/core/node_attrs.h"

namespace camel::core {

namespace module {
class Module;
}

namespace tag {

/// A place a tag may be written (design principles 6.2). A tag lists any of them; a use at a
/// place the tag does not list is an error.
enum Position : uint8_t {
    NoPosition = 0,
    Func       = 1u << 0, // before a function declaration
    Type       = 1u << 1, // before a type declaration
    Field      = 1u << 2, // before a field of a struct type
    Nodes      = 1u << 3, // on a function or a block: the nodes written in it
    Param      = 1u << 4, // before a function parameter (not written yet: H2b)
};
using Positions = uint8_t;

/// A compile-time parameter of a tag (`rule: func`): its name and the type as written.
struct TagParam {
    std::string name;
    std::string type;
};

/// A tag declaration.
struct Decl {
    /// The qualified name: the module's name, `::`, and the name alone. A tag the core itself
    /// declares has no module part (`strict`).
    std::string name;
    /// The module that declares it; empty for a tag of the core.
    std::string module;
    /// The name alone, as the module writes it.
    std::string ident;
    Positions on = NoPosition;
    std::vector<TagParam> params;
    /// An alias group (`tag finite_math = [no_nans, ...]`): the member tags, qualified. A use
    /// of the group is a use of each member. Empty for an ordinary tag.
    std::vector<Reference> aliases;
    /// The passes that read it, as written (`std::opt`, `tensor::fuse`); empty when the tag
    /// says nothing. The pipeline warns when a tag written in the program names no pass that
    /// is in it: the tag then has no effect.
    std::vector<std::string> readBy;
    /// The attribute key of the tag, named as it is qualified (attr::kNoKey for a group). On
    /// nodes it is where the facts are stored; on a declaration it names the fact in the set the
    /// module keeps. Grouped facts combine by the key's rules.
    attr::AttrKey key = attr::kNoKey;
    /// `on nodes` and a C++ declaration: the value for the with-arguments written, nullopt
    /// when the arguments do not give one. Null: the arguments make the value (see above).
    std::function<std::optional<attr::AttrValueId>(std::span<const data::data_ptr_t> args)>
        value;
    /// An alias group is declared but has no facts of its own.
    bool isGroup() const { return !aliases.empty(); }
};

/// A tag's identity; 0 is no tag.
using TagId = uint32_t;
inline constexpr TagId kNoTag = 0;

/// Registers `decl` (or replaces the tag of the same qualified name) and returns its id.
TagId declare(Decl decl);
/// The declaration of `id`; nullptr for kNoTag.
const Decl *get(TagId id);
/// The tag whose qualified name is `name`, when one is declared.
std::optional<TagId> find(std::string_view name);
/// The tag `ident` of the module `module` (empty for the core's own), when one is declared.
std::optional<TagId> find(std::string_view module, std::string_view ident);

/// `func`, `type`, `field`, `nodes`, `param`; empty for none or several.
std::string_view positionName(Position position);
std::optional<Position> positionFromName(std::string_view name);

// ---- C++ module registration ------------------------------------------------------------

/// How a C++ module declares a tag that is written on nodes: it registers the attribute key
/// and, when the tag has a decorator form (`@kernel`), the decorator that grants it. This is
/// the one call that takes the place of attr::registerKey and OperatorTable::declareGrant.
struct NodeTagSpec {
    /// The operator URI of the decorator that grants the tag (`tiles:kernel`), when the tag
    /// may also be written in the `@` form; empty for none.
    std::string decorator;
    /// The attribute value for the with-arguments written; nullopt when they do not give one
    /// (the decorator is then applied when it runs). Null: unit whatever the arguments.
    std::function<std::optional<attr::AttrValueId>(std::span<const data::data_ptr_t> args)>
        value;
    attr::ValueKind kind = attr::ValueKind::Unit;
    /// Null: the inner value wins (what a same-name fact written closer says).
    attr::CombineRule combine;
    /// Null: only equal values may be merged.
    attr::MergeRule merge;
};

/// Declares one node tag: its attribute key, its decorator when it has one, and its rules.
/// `qualifiedName` is the key name (`test.mark`, `tiles.kernel`).
TagId declareNodeTag(std::string_view qualifiedName, Positions positions, NodeTagSpec spec);

// ---- Declaration tags --------------------------------------------------------------------

/// The facts attached to the declaration `name` of `module` (`on func`, `on type`).
attr::AttrSetId ofDeclaration(const module::Module &module, const Reference &name);
/// The facts attached to field `index` of the type declared as `typeName` in `module`.
attr::AttrSetId ofField(const module::Module &module, const Reference &typeName, size_t index);
/// Attaches `tags` to the declaration `name` of `module` (the graph builder does this while it
/// compiles the declaration); `inner` wins over what is there, by each key's combine rule.
void attachToDeclaration(module::Module &module, const Reference &name, attr::AttrSetId tags);
/// Attaches `tags` to a field of a type declaration of `module`.
void attachToField(
    module::Module &module, const Reference &typeName, size_t index, attr::AttrSetId tags);

// ---- Inspection ----------------------------------------------------------------------------

/// Prints what a module declares as tags: each tag with the positions it allows and the passes
/// it names (`read_by`), then the facts attached to its declarations and fields. The form of
/// `camel inspect --tags`; a graph dump shows `on nodes` facts as node attributes (`attrs=`).
void dump(const module::Module &module, std::ostream &os);

} // namespace tag
} // namespace camel::core