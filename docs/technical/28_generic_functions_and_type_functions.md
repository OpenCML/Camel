# Generic Functions and Type Functions

This note describes how generic functions (`typeas`) and type functions (type declarations
with parameters) are compiled. The normative text is `docs/design-principles.cn.md` section
2.9, "类型、泛型与类型函数"; the user-facing description is in `docs/language/` (3.1, 7.5,
7.7). Everything here happens while a module is compiled: no pass, backend or runtime
structure knows about either construct.

## 1. What they are on the graph

| Construct | On the graph |
|---|---|
| generic function `func first(xs: (typeas T)[]): T` | nothing by itself: a subgraph template |
| a call `first([1, 2])` | a call of the subgraph `first<int>`, built once for `T = int` |
| type function `type Pair<T> = (T, T)` | nothing: applied where a type is written |

A graph only ever holds concrete types. `camel x.cml std::rgir` shows one subgraph per set of
types a generic function is called with, named `name<types>`; `camel inspect --gir x.cml`
also prints each subgraph's signature. Types are named as source text names them (design
principles 2.10): `first<int>`, not `first<i64>`. That is the display name only: the mangled
name of a type (`Type::mangle`) and the structural comparison that finds an instance again
do not use it.

## 2. Representation (`include/camel/core/type/typevar.h`)

Two compile-time types, both flagged auxiliary:

- `TypeVarType` (`TypeCode::TypeVar`): a named hole. `binder()` is true for the occurrence
  written `typeas T`; every other occurrence is a use. Occurrences of one name are `equals`.
- `TypeFunctionType` (`TypeCode::TypeFunc`): parameter names and a body, which is an ordinary
  type in which the parameters are `TypeVarType`s.

and the operations over them:

| Function | Meaning |
|---|---|
| `containsTypeVar(type)` | a variable occurs somewhere in `type` |
| `substituteTypeVars(type, bindings)` | replace bound variables; returns the same object when nothing changed |
| `instantiateFunctionType(type, bindings)` | the same for a function type, always a new object |
| `bindTypeVars(pattern, actual, bindings)` | bind the `typeas` occurrences of `pattern` from the type at the same position of `actual` |
| `collectBindableTypeVars(pattern, names)` | the variables `bindTypeVars` can reach |
| `TypeFunctionType::apply(arguments)` | the body with the parameters replaced |

Binding and substitution descend through arrays, tuples, structs and function types.
Substitution also rebuilds unions. Module types (`Tensor<...>`) are opaque: what their
arguments mean is defined by the type's C++ code, which sees concrete types only, so a type
variable among the arguments of a module type is rejected where it is written. The exception
is a type that declares its arguments to be plain types (`OtherType::typeArguments()`, the
built-in `Map<K, V>` and `Set<T>`): binding and substitution descend through its arguments
as they do through an array's element type, and `Map<typeas K, typeas V>` binds both
(`docs/technical/31_map_and_set.md`).

That `substituteTypeVars` returns its argument unchanged when there is nothing to replace
matters: the builders compare types by pointer in places, and code without type variables
must not see new objects.

## 3. The GCT builder (`src/compile/gct/builder.cpp`)

The GCT builder only resolves names; it types nothing.

- `visitFuncData` opens a type scope around a function's signature and body. `typeas T`
  (`visitTypeExpr`) inserts `T` into that scope and yields the binding occurrence; later
  mentions of `T` resolve through the scope like any type name. So `T` is visible in the rest
  of the signature and in the body, and nowhere else.
- `typeas` is allowed only among the parameters of a function *declaration*
  (`funcDeclPending_` / `declSignature_` / `typeasAllowed_`): not in its return type, not in a
  lambda, not in a `let`. A function type written inside such a parameter may bind in both
  positions (`f: (x: T) => typeas U`). To let the return type name the variables, the
  parameters are now visited before the return type.
- After the signature is built, every bound variable must be reachable by `bindTypeVars`
  (`TypeVariableNotBindable` otherwise: a `typeas` inside a union, or passed to a type
  function that drops its parameter).
- `visitTypeDecl` with parameters builds a `TypeFunctionType`; `visitSpecType` applies one
  (`Name<args>`), checking the number of arguments and that all are types. `visitType`
  rejects a type function that is not the base of an application.
- A literal converted to a type written with a variable (`let z: T = 0`, `0 as T`) cannot be
  converted by the GCT builder as it converts literals to concrete types; it becomes a `CAST`
  node (`CastLoad::annotation()` tells the two spellings apart) and is converted when the
  function is instantiated, by the same rule.

A function whose type still contains variables leaves the GCT as an ordinary `FUNC` node
(plus the usual `DECL` at module level).

## 4. The GIR builder (`src/compile/gir/builder.cpp`)

The GIR builder types every node as it creates it and chooses operator overloads at once, so
a body with type variables cannot be built. Instead of making that lazy, a generic function
is not built at all until a call supplies the types, and is then built from its GCT exactly
as an ordinary function is. This is the template model the design principles describe, and
it leaves the node-by-node typing of the builder untouched.

**Templates.** `visitExecNode` calls `registerGenericTemplates` before it builds anything in a
block: each `FUNC` child whose type is still generic under the current bindings
(`isGenericHere`) becomes a `GenericTemplate` holding the GCT node, the declared type and
the scopes of the block (node, graph, decorated and template scopes, owner graph). A call may
therefore precede the declaration. `visitDeclNode` and `visitFuncNode` skip such a function.

**Calls.** `visitLinkNode` marks its callee position (`genericCalleeAllowed_`); a `DREF` that
names a template yields a marker node recorded in `genericDrefs_` there, and
`GenericFunctionAsValue` anywhere else. `resolveGenericCall` then

1. checks the argument counts,
2. binds each variable from the argument type at its `typeas` position (`bindTypeVars`),
3. holds every argument to its parameter with the variables replaced, by the rule for
   ordinary functions (`Type::assignableFrom`; nothing is converted implicitly),
4. asks `instantiateGeneric` for the instance.

The rest of `visitLinkNode` (argument passing, `var` arguments, captured variables, sync
order) runs unchanged on the instance, which is an ordinary graph.

**Instances.** `instantiateGeneric` looks the bindings up among the template's instances
(structural `equals`), so one set of types has one graph. For a new set it

- saves the builder's position (current graph, scopes, flags, bindings) and moves to where
  the function was *declared*, so the body sees the names visible there;
- sets `typeBindings_` to the bindings in force at the declaration plus the new ones;
- creates the graph from the substituted function type, records the instance *before*
  building the body (a recursive call with the same types finds it), and runs
  `buildFuncBody`, the part of `visitFuncNode` that builds a body;
- restores the builder's position.

While `typeBindings_` is set, the three places where a type enters the graph from the GCT
apply it (`boundType`): the type of a `FUNC` node (lambdas and local functions of the body),
the target of a `CAST`, and a `TYPE` node. Inside an instance every `FUNC` node gets a fresh
function type, because a graph completes its type in place (return type, captures) and the
same GCT node is built once per instance.

A generic function declared inside another function is registered when that function's body
is built, with the bindings then in force (`outerBindings`), so it works inside instances
too.

**Errors.** A body is typed at instantiation, so that is when its errors appear. They are
reported at their own position; `Diagnostics::annotateErrorsFrom` appends the instance and
the position of the call that asked for it (at most three levels, then `[...]`). A failed
instance is remembered and later calls with the same types fail without repeating the
diagnostics.

**Bounds.** Instances of *one* template being built inside one another are limited to 8
(`kMaxOpenInstancesOfOne`): `f(x) = f([x])` would never stop, and types may double at each
step (`f((x, x))`). Nested instantiations overall are limited to 128. A recursive call with
the same types is not a new instance and is not limited.

## 5. Exported generic functions

`DESIGN-QUESTION(generic-export)`: where the instances of an imported generic function live is
not ruled on. Implemented: **the template is exported by the module that declares it, and each
importing module builds the instances it calls**. The alternative is one instance per set of
types for the whole program, owned by the declaring module (smaller graphs when several
modules use the same types; the declaring module's graph then changes after it was compiled).

- `export` of a generic function exports its `GenericTemplate` as a module entity of its own
  kind (`Entity::generic_payload_t`, opaque outside the GIR builder; `EntityAccess`).
- A `DREF` that resolves to such an entity is treated like the name of a local template: a
  marker node in callee position, `GenericFunctionAsValue` elsewhere.
- `instantiateGeneric` recognises a template of another builder by its `homeRoot` (the root
  graph of the builder that registered it). For such a template it
  - looks for, and records, the instance under the *importing* builder's root graph
    (`GenericInstance::owner`), so two importing modules each have their own;
  - creates the instance graph under the importing module's root graph;
  - moves to the template's scopes as for a local template, and also to its module
    (`module_`), so that names the body imports are looked up in the declaring module;
  - enters a scope whose name carries the importing root, because scopes are kept by name
    under the scope they are entered from, which here belongs to the declaring module.
- The body may call private functions, other generic functions and imports of its module.
  It may not use a top-level *value* of its module: that node lies in a graph that does not
  enclose the instance, so it cannot be captured (`ImportedFunctionUsesModuleValue`, located
  at the function; the same diagnostic an ordinary imported function gets). A constant a
  library needs is written as a function.
- An error in the body is reported in the declaring module's file
  (`Diagnostics::locateByOriginFrom`), with the call that asked for the instance.

## 6. Limits of this implementation

- A generic function has no function value: it cannot be bound to a name, passed as an
  argument or decorated (`GenericFunctionAsValue`, `GenericFunctionUnsupported`).
- One name is either a generic function or ordinary functions; two generic functions with
  one name are a `Redeclaration`.
- A generic function that is never called is never instantiated, so its body is never typed.
- Type arguments cannot be written at a call; they always come from the arguments.
- Type functions take types only (no `const` arguments), and their right-hand side is an
  ordinary type expression; they cannot refer to themselves.
- Composite types are still not canonical: two instantiations of `T[]` are distinct objects
  compared with `equals`.

## 7. Tests

`test/plans/feat/compile/generics.plan.toml` (cases under `test/cases/compile/generics/`):
behaviour, the four backends and the `std::opt` / `std::inline` / `std::macro` pipelines, the
shape of the graph, and each diagnostic with its position.
