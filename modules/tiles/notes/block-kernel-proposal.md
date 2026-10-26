# Block kernel interface proposal

Status: discussion draft for the language author. No syntax or semantics in this
document are implemented or approved by its existence.

## Goal and representation

Expert kernels should be ordinary Camel function graphs over logical blocks,
sharing operation declarations, transformations and differentiation rules with
tensor programs. A block specifies a bounded logical domain, element type and
validity information. It does not specify a CUDA block, warp, register layout or
address space. Physical scheduling is a separate Tiles input.

`@kernel` and `Tile<T, ...>` are provisional surface spellings only. An initial
prototype can be expressed using graph APIs once their contracts are stable;
there is no need to introduce an independent kernel-language parser.

## Decisions requested from the author

- Is a logical block an ordinary shaped tensor value plus domain constraints, or
  a distinct type? Avoid duplicating all tensor operators without a semantic need.
- How are logical extents, compile-time capacities and valid boundary masks
  represented? A validity mask must not silently become a numerical zero value.
- Which operations make masked accesses safe? Distinguish a masked load from an
  eager load followed by selection. Specify inactive-lane and empty-reduction
  behavior, including the neutral element or explicit fallback when required.
- Does the kernel marker merely select compilation, constrain admissible graph
  operations, or both? It must not grant numeric permissions implicitly.
- How are shape parameters bound at launch, and how are invalid bindings reported?
- Is differentiation defined for the logical value graph only? Physical barriers
  and address layouts should not acquire accidental differentiation rules.

The existing lexical numeric-permission rule remains authoritative: permission
does not propagate implicitly into called functions and must survive expansion.

## Execution and checking

A CPU evaluator can serve as a reference only when it implements the declared
logical operations and numeric contract. Existing optimized tensor CPU kernels
are not automatically that evaluator. Basic strict operations require bitwise
checks; transcendental functions require their declared ULP checks. Independently
checking exceptional values and masks is necessary even when a tolerance test
passes. A physical execution backend need not be differentiable; the logical
graph requires valid differentiation rules for its constituent operations.

The launch boundary binds logical inputs/outputs, extents and resource identities.
Tiles maps the logical iteration space to blocks/threads, allocates local storage
and establishes barriers and completion dependencies. Verify bounds, races,
barrier participation, initialization and resource lifetime before launching.

## First vertical slice

Use a row-wise softmax logical graph: maximum, subtraction, exp, ordered sum and
division. Preserve the input graph's rounding points and accumulation type. Test
full and partial rows, empty domains under the eventual declared rule, exceptional
values, masked accesses and differentiation of the logical definition. Compare
one fused physical kernel against the same logical reference; benchmark only
after semantic verification. Larger rows may require a different validated plan.

This proposal does not require language-level CUDA thread IDs or CUDA-specific
types. Explicit physical scheduling can be a separate expert interface whose
constraints are checked against the same logical graph.
