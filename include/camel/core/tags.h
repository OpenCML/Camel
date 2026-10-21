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

#include <cstddef>
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

namespace camel::runtime {
class GCGraph;
class GraphDraft;
using gc_node_ref_t = uint32_t;
} // namespace camel::runtime

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
    /// A standard numeric-permission tag (design principles 7.2): the permission it grants, as
    /// a `sem::PermissionMask`; 0 when the tag grants no permission. Writing the tag on a
    /// function or a block grants the permission to the code written there, exactly as
    /// `@numeric(...)` does. `finite_math` is a group of the four assumptions and has 0 here;
    /// its members carry their bits.
    uint8_t permission = 0;
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

/// Declares the tags the language itself names, once per process: the numeric permissions of
/// design principles 7.2 (`reassociate`, `contract`, `approx_math`, `change_precision`, the
/// four finite-math assumptions and their group `finite_math`). They are names of the core, so
/// a program writes them without importing anything. Idempotent.
void declareStandardTags();

/// The numeric permission a tag grants; 0 for a tag that is not a permission, and for the
/// `finite_math` group (whose members carry the four assumptions).
uint8_t permissionOf(TagId id);

/// The numeric permission the tag `name` grants, resolving the core's standard tags and the
/// group `finite_math`; 0 when `name` is not a permission tag (including an undeclared name).
uint8_t permissionOfName(std::string_view name);

/// The `read_by` passes of the tags in `tags`, sorted and without repeats. An empty list when no
/// tag in the set names a reader.
std::vector<std::string> readersOf(attr::AttrSetId tags);

// ---- Readers ------------------------------------------------------------------------------

/// Registers `pass` as a pass that reads tag facts (`std::opt`, `tensor::fuse`, ...). A tag's
/// `read_by` names such a pass; `isReader` answers whether a written name is one. Registering
/// the same name twice is harmless.
void declareReader(std::string_view pass);
/// Whether `written` names a registered reader: the registered pass itself or a path below it
/// (the registered `std::opt` matches `std::opt::inline`).
bool isReader(std::string_view written);

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

/// The facts of a declaration or a node, read through one query (design principles 6.2, 7.5):
/// the tags written on it, the numeric permissions it grants, and the passes its tags name as
/// readers. A pass or a C++ module reads all three here instead of reaching for the attribute
/// set and the permission column separately.
struct Facts {
    /// The tags written on the declaration (or the node attributes), as an attribute set.
    attr::AttrSetId tags = attr::kNoAttrs;
    /// The numeric permissions granted (`sem::PermissionMask`): the permission tags written on
    /// the declaration, and for a function the permissions of its body.
    uint8_t permissions = 0;
    /// The `read_by` passes of the tags present, sorted and without repeats.
    std::vector<std::string> readers;
};

/// The facts of the declaration `name` of `module`, over every overload of that name.
Facts factsOf(const module::Module &module, const Reference &name);
/// The facts of one overload of `name`: `signature` is the declaration's function type as
/// written (`(int) => int`); empty for a declaration that cannot be overloaded.
Facts factsOf(const module::Module &module, const Reference &name, std::string_view signature);
/// The facts of field `index` of the type declared as `typeName`.
Facts factsOfField(const module::Module &module, const Reference &typeName, size_t index);
/// The facts of node `node` of a graph being built (the draft) or of a runtime graph: its
/// attributes and its numeric permissions.
Facts factsOf(const camel::runtime::GraphDraft &draft, camel::runtime::gc_node_ref_t node);
Facts factsOf(const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t node);

/// The facts attached to the declaration `name` of `module` (`on func`, `on type`).
attr::AttrSetId ofDeclaration(const module::Module &module, const Reference &name);
/// The facts attached to the overload `signature` of the declaration `name`.
attr::AttrSetId ofDeclaration(
    const module::Module &module, const Reference &name, std::string_view signature);
/// The facts attached to field `index` of the type declared as `typeName` in `module`.
attr::AttrSetId ofField(const module::Module &module, const Reference &typeName, size_t index);
/// Attaches `tags` to the declaration `name` of `module` (the graph builder does this while it
/// compiles the declaration); `inner` wins over what is there, by each key's combine rule.
void attachToDeclaration(module::Module &module, const Reference &name, attr::AttrSetId tags);
/// Attaches the facts of one overload: `signature` distinguishes the overloads of an operator.
void attachToDeclaration(
    module::Module &module,
    const Reference &name,
    std::string_view signature,
    attr::AttrSetId tags);
/// Attaches `tags` to a field of a type declaration of `module`.
void attachToField(
    module::Module &module, const Reference &typeName, size_t index, attr::AttrSetId tags);
/// Records the numeric permissions the declaration `name` grants (its body's permissions).
void attachPermissionsToDeclaration(
    module::Module &module,
    const Reference &name,
    std::string_view signature,
    uint8_t permissions);

// ---- Inspection ----------------------------------------------------------------------------

/// Prints what a module declares as tags: each tag with the positions it allows and the passes
/// it names (`read_by`), then the facts attached to its declarations and fields. The form of
/// `camel inspect --tags`; a graph dump shows `on nodes` facts as node attributes (`attrs=`).
void dump(const module::Module &module, std::ostream &os);

} // namespace tag
} // namespace camel::core