# Generic Functions and Type Functions

This note describes how generic functions (`typeas`) and type functions (type declarations
with parameters) are compiled. The normative text is `docs/design-principles.cn.md` sections
2.9, "类型、泛型与类型函数", and 2.10; the user-facing description is in `docs/language/` (3.1, 7.5,
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
| `instantiateFunctionType(type, bindings)` | the same for a function type, always a new open signature (`OpenFunctionType`): the type of an instance about to be built, whose body may still decide the result type |
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

**Calls.** A `DREF` that names a template yields a marker node recorded in `genericDrefs_`,
wherever it stands. For a call, `resolveGenericCall` collects what all arguments say about the
variables and then solves, at the call and nowhere else:

1. it checks the argument counts;
2. each variable takes the type of the argument at its `typeas` position (`bindTypeVars`; an
   argument that is not of the form of a `T?`/`T!` parameter enters it and binds the payload);
3. a variable that position left open takes the type found at any other occurrence
   (`unifyTypeVars`): `or_zero(null, 4)` binds `T` from the second argument;
4. the arguments that name a generic function themselves wait for this. For each, the
   parameter it is passed to, with what is known so far, is the function it has to be
   (`instantiateGenericAs`); its instance then says what was still open in that parameter
   (its return type), which may let the next one go. This repeats while it makes progress;
5. a variable or an argument left over is an error that names the variables
   (`GenericArgumentsMismatch`);
6. every argument is held to its parameter with the variables replaced
   (`entersImplicitly`), and `instantiateGeneric` is asked for the instance.

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

All modules of a program are built by one graph builder into one root graph (design
principles 2.11; `src/compile/gir/program.cpp`). An instance of a generic function is
therefore **one graph per set of types for the whole program**, under the graph the function
was declared in (the root graph for a function of a module's top level), whichever module
calls it. (Before the root graphs were merged, each importing module built instances of its
own and their bodies could not use a top-level value of the declaring module; that was
`DESIGN-QUESTION(generic-export)`, which the merged root settles.)

- `export` of a generic function exports its `GenericTemplate` as a module entity of its own
  kind (`Entity::generic_payload_t`, opaque outside the GIR builder; `EntityAccess`).
- A `DREF` that resolves to such an entity is treated like the name of a local template: a
  marker node, instantiated by a call or by the position it stands in (section 6).
- `instantiateGeneric` looks the instance up in the template's instance table; to build one
  it moves to the template's scopes and to its module (`module_`), so that names the body
  imports are looked up in the declaring module, and creates the instance graph under the
  template's `ownerGraph`.
- The body may call private functions, other generic functions and imports of its module,
  and use the top-level values of its module: they are nodes of the root graph, which
  encloses the instance, and are captured like any value of an enclosing graph.
- An error in the body of an instance asked for by another module is reported in the
  declaring module's file (`Diagnostics::locateByOriginFrom`), with the call that asked for
  the instance.

## 6. A generic function where a function is expected

Design principles 2.10: a generic function may stand wherever a function is expected, and is
instantiated once the position fixes its type variables. The delay exists while the module is
compiled: the marker node of the name is replaced by the function value of an instance
before the call it belongs to is built, and no graph ever holds a template.

| Position | What fixes the types | Where |
|---|---|---|
| argument of a function with one signature, of a function value | the parameter's function type | `settle` in `visitLinkNode` -> `genericValueFor` |
| argument of a generic function | the solver above | `resolveGenericCall` |
| `let f: (x: int) => int = id`, `id as (x: int) => int` | the written type | GCT emits a `CAST` for a declared function type; `visitCastNode` resolves it and adds no node |
| `return id` | the declared return type | `visitExitNode` |
| argument of a built-in operator (`map`, `filter`, `reduce`, ...) | trial, see below | `settleGenericOperatorArguments` |
| anything else (`let g = id`, an array element) | nothing: `GenericFunctionAsValue`, naming the variables | `rejectNameWithoutValue`, `visitNRefNode` |

`instantiateGenericAs(tmpl, expected)` binds the template's variables from the parameter
types of the expected function type (`unifyTypeVars`), requires the instance to have exactly
those parameters, builds it, and compares its return type when the expected one is known.

**Built-in operators.** The type rule of an operator is C++ code (a resolver) that is handed
the argument types and answers yes or no; it cannot be asked which function it expects. So
the instances the other arguments suggest are tried. The candidate types for the parameters
of the function are what those arguments are made of (the element type of an array or a set,
the key and value types of a map and their pair) and then their own types; every choice of
candidates that unifies with the template's parameters and fixes all its variables gives a
function type. The operator is asked about that type *before* the instance is built (with
the declared return type, or, when none is declared, with the return types an element
function usually has), so that the body of an instance the operator would refuse is never
built and cannot report errors for types the program does not mean. The first instance the
operator accepts is taken. An instance that was built and then refused (its result type is
not what the operator needs) stands for the name, so that the operator's own diagnostic says
what does not fit. A name with several ordinary functions (`xs->map<abs>`,
`xs->reduce<max, 0>`) is settled the same way: the operator is asked about the type of each
function of the name (for an operator with a computed signature, about its signatures for
the candidate types), and exactly one accepted is the argument; none or several leave the
name without a value (`FunctionNameAsValue`, listing the candidates). Not covered: an
operator whose required result type is none of the usual ones may refuse a generic function
it could take; a lambda with written types always works.

**Functions written in place.** A `FUNC` node without a name whose type still holds a
variable is registered where it stands (`anonymousTemplateOf`) and yields the same marker.
Programs cannot write one (`typeas` is refused in a lambda); the list comprehension does:
`[e for x in xs if c]` is lowered by the GCT builder (`visitComprehension`) to
`xs->filter<F>->map<G>` with `F`, `G` taking `x` of a fresh type variable, which the two
operators fix as above. The graph is the one the two calls written out have. The two
operators are named with a reserved prefix (`$builtin::map`) that `visitDRefNode` resolves in
the built-in module without looking at the program's declarations, so a function of the
program called `map` or `filter` does not change what a comprehension means.

## 7. Limits of this implementation

- A generic function cannot be decorated (`GenericFunctionUnsupported`), and `f<w>` with a
  generic `f` is not a partial application (`PartialApplicationRefused`).
- One name is either a generic function or ordinary functions; two generic functions with
  one name are a `Redeclaration`.
- A generic function that is never called is never instantiated, so its body is never typed.
- Type arguments cannot be written at a call; they always come from the arguments.
- Type functions take types only (no `const` arguments), and their right-hand side is an
  ordinary type expression; they cannot refer to themselves.
- Composite types are still not canonical: two instantiations of `T[]` are distinct objects
  compared with `equals`.

## 8. Tests

`test/plans/feat/compile/generics.plan.toml` (cases under `test/cases/compile/generics/`):
behaviour, the four backends and the `std::opt` / `std::opt::inline` / `std::macro` pipelines, the
shape of the graph, and each diagnostic with its position. Generic functions as values:
`test/plans/feat/consistency/generic_values.plan.toml`; list comprehensions:
`test/plans/feat/consistency/comprehension.plan.toml`.
