# NN Autograd Prototype

This prototype keeps trainable weights in mutable `Parameter` objects and lets
models group parameters with ordinary Camel structs. Forward code can use
`with<...>` context parameters so layer composition stays pipeline-friendly:
`x->layer1<model>->layer2<model>`.

Reverse-mode synthesis is centered on a VJP registry:

- Builtin primitive rules are registered by URI, for example `tensor:matmul`,
  `tensor:sum`, `:op/div_d`, and `nn:value`.
- `compile_step` builds a forward tape for the current linear-MSE demo, seeds
  the loss cotangent with `1.0`, and walks the tape backward through the VJP
  table to emit gradient nodes.
- `nn:value(Parameter) -> Tensor` is the trainable leaf rule. Its VJP records a
  `Parameter` gradient, and the generated step emits `zero_grad`, `add_grad`,
  and `sgd` control nodes.
- User functions can be annotated with `@vjp<rule>`. The decorator macro
  records the target function graph name and VJP rule graph in the registry,
  then returns the original function unchanged.

The current generic boundary is intentionally narrow: builtin VJP rules are
used for the demo tape, while registered user-function VJPs are stored as
metadata for the next step, where call nodes in arbitrary loss graphs can be
lowered through the same registry.
