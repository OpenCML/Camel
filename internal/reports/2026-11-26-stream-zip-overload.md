# `stream.zip` cannot be called by its own name: it collides with `prelude.zip`

Found by the doc-example compiler (`--verify-docs`) while compiling
`src/stdlib/stream.cml:164`'s example, which is the only place the module's own
`zip` is instantiated.

## The diagnostic

```
(main)/tmp/camel-doc-example-zPiFvo/main.cml:3:25: [Error]:
The call to 'zip' with (int.., string..) fits more than one overload. More than
one function takes these arguments; make the call say which
```

## Why no caller can use it now

`stream.cml:165` declares `zip::<T, U>(a: T.., b: U..) => (T, U)..`, and
`prelude.cml:1295` declares `zip::<...A>(...xs: ...A) => (...ElementOf<A>)[] panic
when ...is_array::<A>()`. The prelude's `zip` is unconditional as far as overload
resolution is concerned — its `when is_array::<A>()` is a condition on the
operator, and the resolver does not use it to drop a candidate — so a call whose
arguments are two streams fits both functions. A module's own name therefore does
not shadow the prelude's, and `stream`'s `zip` is unreachable except through a
qualified/builtin spelling.

This is the same shape as `array.enumerate`: a declaration whose body or name
expression the language does not actually resolve, invisible until something
instantiates it.

## Ways to fix it (needs a design decision, not a doc edit)

1. Overload resolution uses a candidate's `when` condition when it can be decided
   at the call site (`is_array::<A>()` is false for `(T.., U..)`), which removes
   the ambiguity and is the general answer.
2. A module's declaration shadows the prelude's name of the same name in that
   module and in programs that import it (a scoping rule, design principles 6.2/6.4).
3. `stream.zip` is renamed (e.g. `zip_streams`), which sidesteps the rule change
   but leaves the shadowing question open.

Until one is decided, the example stays red and is named here rather than recorded
as an "example that cannot compile": the example is right, the resolution is not.