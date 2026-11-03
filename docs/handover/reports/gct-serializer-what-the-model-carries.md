# What a GCT serializer has to carry, counted from the model

The ruling's first commit is a structured writer and reader of the construction tree, and the last
two rounds established the two things that decide its shape: the printed dump is a rendering (a
`TYPE:` line is a spelling, a `DATA:` line is a value with its address tokenized away), and a reader
that parses that rendering would reproduce the rendering and nothing else. So the format has to be
written against the model. This is what the model actually carries, counted from
`include/camel/compile/gct/tree.h`.

**A node** is a kind, a name, its children (`args`), and a payload that depends on the kind (a branch
kind, a case pattern, a static-loop's names, an access key, and so on). **Beside** the nodes, the tree
keeps fifteen tables keyed by the node itself:

| table | key | what it holds |
| --- | --- | --- |
| `tagUsesOf` | `Node*` | the tags a node was written with |
| `fieldTagUsesOf` | `Node*` | the per-field tags of a declaration |
| `operRefOf` | `Node*` | the operator a node names, by `OperId` |
| `usingsOf` | `Node*` | the `using namespace` in force at that node |
| `namespaceOf` | `Node*` | the namespaces of an operator overload's name |
| `literalValueOf` | `Node*` | the exact value as written (`0.1` is not the nearest double) |
| `literalElementsOf` | `data_ptr_t` | one value and origin per element of a composite literal |
| `unwrittenParamsOf` | `Node*` | the lambda parameters whose type was left out |
| `tupleParamsOf` | `Node*` | the parameters destructured as tuples |
| `whenOf` | `Node*` | the declaration's applicability condition and its text |
| `caseGuardOf` | `Node*` | a `match` case's guard, which is not a child of the case |
| `headTypeParamsOf` | `Node*` | the type parameters written at a declaration's head |
| `parameterDefaultsOf` | `Node*` | each parameter's default, which is not a child of the function |
| `argumentNamesOf` | `Node*` | the names a call wrote on its arguments |
| `nativeValueTypeOf` | `Node*` | the value type a native declaration provides |

Three consequences follow, and they are the whole of the format's design.

**The keys are addresses, so the file keys by index.** Fourteen tables are keyed by `const Node*` and
one by `data_ptr_t`; none of those key values can be written. The writer numbers the nodes in one
traversal, writes every table entry against that number, and the reader builds the nodes first and
then fills the tables by number. A table entry whose number is not in the file is a corrupt file, not
a warning to write down and continue past.

**Operator identity and type identity are not pointers either.** `operRefOf` holds an `OperId` and
payloads hold `Type*`; both are resolved through tables, so the file has to carry a type table keyed
by a canonical identity (design principles 5.1 gives every type one) and an operator reference keyed
by the same name the registry resolves, not by an address. Two different operators that share a
spelling is the case that goes wrong if this is done by name alone.

**The constant table is a table.** Literal values live in a deque beside the tree with their exact
values and the elements' origins; a composite literal's elements are reachable only through it. It is
written as a table indexed like the nodes, not as values inlined wherever they happen to be used.

**Ten identity assertions**, each of which must go red when the reader drops the field it tests, and
none of which `print` can see: the origin of a node; the exact value of a numeric literal; the value
and origin of a composite literal's element; the operator a node names when two operators share its
spelling; the tags of a node and of a field; the `using namespace` in force at a node; the namespaces
of an operator overload's name; a declaration's `when` condition and a case's guard (both are outside
the child list, so a children-only writer loses them silently); a parameter's default and a call's
named arguments; and a node that two references share, which must come back as one node and not as
two copies (the tree can share a subtree; `clone` exists for the case where it must not).

The criteria for the commit stay the three the author set: `std::gir`'s sha256 the same after the
round trip as before it, the printed text the same as a cross-check rather than as the judge, and the
round trip run twice to the same result, with a version bump refused and the fallback's graph sha
unchanged. Until the file exists, none of those has a reading, and nothing in this page makes a
program faster: there is no stored module and `imports_nothing` does not move.