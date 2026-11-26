# `array.enumerate` cannot be instantiated: its body calls `zip` with a range

Found by the doc-example compiler (`--verify-docs`) while compiling
`src/stdlib/array.cml:66`'s `@example ['a', 'b']->enumerate`.

## The diagnostic

```
(main)/home/zjwei/camel-work/stdlib-reds/out/latest/stdlib/array.cml:68:15: [Error]:
No candidate of 'zip' applies to (<> (Range, string[])): every candidate's 'when' condition is false
```

## Why no caller can use it now

`enumerate` is declared as

```camel
export func enumerate::<T>(xs: T[]) => (int, T)[] {
    return zip(0 .. len(xs), xs)
}
```

`0 .. len(xs)` has type `Range<int>` (`int..`), and the only `zip` in the library
is `prelude.cml`'s `zip::<...A>(...xs: ...A) => (...ElementOf<A>)[] panic when
...is_array::<A>()` — every candidate requires each argument to be an array, so
`is_array::<A>()` is false for the pair `(Range<int>, T[])` and the call has no
candidate at all. The generic body is never instantiated by the library itself, so
the build does not see it; any program that writes `xs->enumerate` gets the
diagnostic above.

## Ways to fix it (needs a design decision, not a doc edit)

1. `zip` gains an overload that takes a range and an array (`zip(r: int.., xs:
   T[])`, materialising the range), which is also what a user would expect from
   `enumerate`'s name.
2. `enumerate` stops going through `zip`: a `loop` over `0 .. len(xs)` building
   `(i, xs[i])`, which needs no new operator and keeps `zip` as it is.
3. `enumerate` is removed from the module until one of the above is decided.

This is a semantic/overload-resolution matter: option 1 changes what `zip`
accepts, option 2 changes a library function's body. Both belong to the library
and the design principles (5.1 entering, 6.2 overloads), not to the documentation
lane that found it — the example is correct, the function is not.