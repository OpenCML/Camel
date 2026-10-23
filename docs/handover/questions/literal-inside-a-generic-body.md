# A literal written inside a generic function whose element type is open

Author: the H371 agent (literals beside a tensor), 2026-10-24. Status: open, needs the coordinator
(or the language author).

## Problem

Design principles 5.1 gives a literal operand the element type of the other operand, and the
literal rule now does that in the operator's own resolution (a literal beside an `f32` tensor is an
`f32` value there, in an infix operator and in a named call alike). A literal written inside a
**generic** function cannot be typed that way:

```
func gate(t: Tensor): Tensor { return if t[0] > 0.5 then relu(t) * 2.0 else t * (t[1] * 2.0) - 1.0 }
```

`t`'s element type is open where `0.5` is typed (`Tensor` fixes no element type), so the literal
keeps its default `f64`, and the tensor operators keep the answer they have always given for a
constant: they compute in the element type and the kernel narrows it. When the *call* fixes the
type (`gate(x)` with `x: Tensor<f32, [4]>`), nothing re-types the body's literals, and every later
step that reads the graph again under the concrete type sees an `f64` constant beside an `f32`
tensor: the ONNX exporter (`export_model` of such a function) writes a `Cast` for it, exactly as it
did before the literal rule, and the tiles planner refuses the device scope that carries it
(`scalar_tensor_operand`), which is the `#gpu` goal this branch is for.

Measured instances: `exemplars/08_structured_control_flow/program.cml` (`gate`), and
`test/cases/modules/onnx/export_var_compound.cml` (`forward(x: Tensor)` with `x * s`).

## What is not decided

1. **Re-type at instantiation**: when a generic body is instantiated with a concrete element type,
   re-type the literal constants in it the way the literal rule would have (`LiteralNotRepresentable`
   where the type cannot hold them). This is the front end's monomorphization, and it also decides
   whether the *oracle* for "the literal rule already ran" is the operator's resolution or the
   instantiation.
2. **Keep the tolerance and let the consumer refuse**: the graph carries the `f64` constant, the
   exporter writes its `Cast`, and a device scope that needs it is refused by the planner that
   knows the device's element type. Nothing new is reported; the `#gpu` goal stays unreachable for
   a program written this way, and the program writes `f32(0.5)` to reach it.
3. **Neither**: 5.1 says the *expected type* comes first, and an open element type is not an
   expected type; a literal in a generic body could be an error until the type is fixed, which
   would refuse programs that compile today.

## Why it is a question and not a change here

This branch carries the literal rule in the operator's resolution and the refusal for a scalar
*value* at a call the program wrote (`InferContext::sourceLevel()`); a pass that reads the graph
again infers without that flag and keeps the tolerance the program was compiled under. Making the
literal rule reach into an instantiated generic body is monomorphization work, and it is the one
place where the two would meet: the choice decides whether the tolerance stays (option 2, what this
branch does) or whether instantiation re-types (option 1).

A fourth piece of the same question is a global one: the reviewer's own note is that option 1
"must not add device-side `f64` scalar support", so a `f64` constant a program *writes* beside an
`f32` tensor is narrowed on the host either way, and the graph that carries it is a host graph.

## What blocks on it

Nothing in this branch: the rule, the refusal and the standard library were verified green with the
tolerance in place (whole fast layer, `compile`, `modules`, `tiles` and the GPU plans' device-free
entries). What it decides is whether a `#gpu` scope written as a generic function that scales by a
literal can compile, which is the author's goal for the follow-up.