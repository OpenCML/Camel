# Core semantic declaration migration inventory

Status: implementation inventory and proposed migration boundary, 2026-10-15.
Based on compiler.cpp, schedule.cpp and codegen.cpp at b710916a, including the
strict matrix-product correction. This does not define new language semantics.

## Boundary

Camel graphs and registered operation interfaces are the authoritative semantic
representation. Derived domains, shapes, alias information and numeric permissions
belong to dependency-validated graph facts. Tiles owns target-specific layouts,
tiling, thread assignment, synchronization, placement, storage and tuning plans.
A transient lowered representation may contain references to semantic information;
it must not become a second independent definition of those semantics.

The compiler already dispatches original graph operations using core OperId.
The following strings are private lowered Node.op tags, not graph operator IDs.
Replacing graph URI dispatch did not remove this private representation.

## Complete current lowered-tag inventory

| Tags | Domain/access requirements | Numeric/reduction requirements |
| --- | --- | --- |
| `input` | Dense logical shape; host snapshot or borrowed device allocation | f32 tensor or resident f64 scalar; no computation |
| `constant64` | Scalar domain | Exact stored f64 bit pattern |
| `cast_f32` | Scalar identity mapping | Explicit f64-to-f32 conversion boundary |
| `scalar_add`, `scalar_sub`, `scalar_mul`, `scalar_div`, `scalar_neg` | Scalar domain, one or two inputs | Ordered f64 arithmetic; scalar division faults on either signed zero |
| `neg`, `abs`, `exp`, `log`, `sqrt`, `rsqrt`, `relu` | Output domain mapped to one input with broadcasting | Preserve f32 node boundaries; exp/log need ULP bounds; rsqrt currently means division by sqrt, not an approximate reciprocal-square-root instruction |
| `add`, `subtract`, `multiply`, `divide`, `maximum`, `minimum` | Right-aligned broadcast maps for two inputs | f32 boundaries; tensor divide follows tensor behavior, not the scalar fault rule; extrema use ordered comparisons, not an unspecified mathematical max/min |
| `reshape` | Equal element counts; flatten/unflatten with quotient and remainder | No arithmetic conversion |
| `reindex` | Output coordinate -> input linear offset + sum(coordinate * stride); zero strides support broadcast | No arithmetic conversion; valid-domain/overflow verification; currently nonnegative offsets/strides |
| `sum_all`, `mean_all` | Full input domain -> scalar; row-major iteration | f32 inputs accumulated in f64 from +0 in order; f64 result; mean divides by count; empty full mean rejected |
| `sum_axis`, `mean_axis` | Parallel coordinates outside one axis; ordered reduction coordinate on that axis; keep-dimension encoded in output shape | f64 accumulator, final f32 conversion; mean division before conversion; empty-axis behavior needs an explicit declaration |
| `max_axis` | Same axis domain; empty reduction axis rejected | Start from first element; replace only when current < candidate; first NaN retained, later NaNs ignored, first equal value/zero retained |
| `sum_to` | Reduce leading and broadcast-expanded dimensions to target shape; preserve remaining coordinates | Row-major f32 accumulation from +0; differs from sum_axis accumulation type |
| `matmul` | Rank-2 A(i,k), B(k,j), C(i,j); i/j parallel and k ordered; masked physical tile edges | Continuous f32 FMA chain in increasing k order; +0 initial accumulator; zero K performs no products |

These are 30 lowered tags. Shape, dtype, inputs, generic integer attributes,
offset and strides currently carry their parameters. Ordinary tags have no
generic integer attributes. Axis reductions use attributes[0] for the normalized
axis; constant64 uses attributes[0] for the f64 bit pattern.

## Source operations normalized before scheduling

| Tensor definition name | Current normalization |
| --- | --- |
| `sum`, `mean` returning float | sum_all / mean_all |
| `transpose`, `permute`, `slice` | reindex with computed output shape, offset and strides; slice currently requires positive step |
| `broadcast_like` | reindex with zero strides on expanded dimensions |
| `expand_axis` | optional reshape followed by broadcast reindex |
| `ones_like`, `zeros_like` | scalar f32 input plus broadcast reindex |
| `cast_like` | identity for the supported equal-shape f32 case; unsupported cases rejected |
| `sum_to` | identity for equal shapes, otherwise explicit reduction |
| `matmul_grad_lhs`, `matmul_grad_rhs` | rank-2 transpose maps plus matmul |

Core declarations should describe these operations before normalization as well
as their valid decompositions. The scalar source IDs are :op/add_d, :op/sub_d,
:op/mul_d, :op/div_d and :op/neg_d. Tensor declaration URIs come from OpRegistry;
the table above deliberately uses definition names rather than inventing URI
spellings for overloads.

## Integration sequence

1. Agree on structural domain/access-map fields, semantic versioning, numeric
   permissions and dependency validation. Migrate broadcasting, transpose,
   reductions and contraction first. Unknown information remains conservative.
2. Build a structured physical kernel representation: loops, typed scalar
   expressions, masked loads/stores, local storage and barriers. Printing CUDA is
   its terminal serialization, not its optimization interface.
3. Implement reduction-region fusion with strict ordered accumulation first.
   Parallel reassociation requires explicit permission. Fuse softmax/layernorm
   expressions without silently replacing them by a different numerical formula.
   Single-kernel execution is a target for suitable row sizes, not a requirement
   to exceed resource limits or to change the algorithm for all shapes.
4. Add the Tiles graph pass once region replacement, shape bindings, effect/fault
   preservation and ownership boundaries have stable interfaces. Preserve the
   existing explicit run/source/inspect entry points.
5. Specialize by static type facts when available; bind symbolic extents at
   runtime. Optional guarded variants may improve performance without treating
   every scalar value or every batch size as a compilation parameter.

Cache lookup should precede CUDA printing using a canonical region fingerprint,
semantic/decomposition versions, numeric permissions, target/compiler identities
and physical strategy. Runtime data belongs outside the key unless an explicit
specialization has made it a semantic constant.
