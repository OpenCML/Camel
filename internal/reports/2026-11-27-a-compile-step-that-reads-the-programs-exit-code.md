# A compile step that reads the program's exit code

**What is left red, and why the pages are not the ones to change.** Two reader-facing
fences are reported as `does not compile`: `docs/design-principles.cn.md:131` and
`docs/design-principles.en.md:134`. Both hold the same example, a graph of two nodes in
which `main` returns `b`, and `b` is `3` (`log(1)` → `2`, `log(2)` → `3`). The example
compiles. The message the checker prints is not even a diagnostic:

```
does not compile: digraph GraphIR {
```

That line is the head of the DOT dump the graph pass prints on stdout, which is where
`firstDiagnostic` falls back when the output carries no diagnostic at all
(`scripts/check-doc-snippets.mjs:637`). So the verdict came from the exit code alone.

## The two rules that collide

The checker states what its compile step is for, and it is deliberate:

> Otherwise the snippet only has to compile: parse, GCT and GIR, asserted with
> `camel <file> std::gir`.
> — `scripts/check-doc-snippets.mjs:25`

The language states what `main`'s result is, and it is deliberate as well:

> `main` returns an exit code (`int`, `bool`, `byte`: the types the process exit code is
> derived from): the value is the exit code, as for any `main`.
> — `docs/cli.md:151`

Both hold only if `camel <file> std::gir` is a compile and nothing else. It is not: the
process exit code of the graph pass is the program's own result. Measured on three
programs that differ only in what `main` returns, with the same command line
(`camel <file> std::gir`):

| program | stdout | exit |
| --- | --- | --- |
| `func main() => int { return 0 }` | the DOT dump | 0 |
| `func main() => int { return 7 }` | the DOT dump | 7 |
| the design-principles example (`main` returns `3`) | the DOT dump | 3 |

The third row is the page's example, unchanged. A page whose entry returns a non-zero
value therefore cannot pass a check that reads the exit code as a verdict, however
correct the page is. The same mechanism is why a fence whose entry the graph pass has to
finish cannot pass either: the constant index past the end and the recursion over the
limit on `docs/language/04_execution_model.md` are reported as broken code, not as the
runs their text blocks show.

## The three page-side routes, refused one by one

- **`fragment`** — "a piece of a program"; the reason must name what is missing. Nothing
  is missing from this fence: it is a whole program, and it is right. Marking it would put
  a false statement on the page.
- **`counter-example`** — "deliberately wrong". This example is not wrong, it is the
  chapter's illustration of how a graph is entered. The marker would also demand a label
  word on the page (`反例`, `错误`, `wrong`, `does not compile`, …), so the page would have
  to say something untrue a second time.
- **`compile-only`** — a text block that is not this run's output; the fence is still
  compiled, so the exit code is still read. It changes nothing.

A fourth route exists and was rejected as well: adding a `// camel …` line to the fence so
the run step takes over. The run step expects exit `0` unless the text block declares a
diagnostic, so the same non-zero result would fail there instead — and the fence is
presented as the content of a file the page links, so it must not gain a line the file
does not have.

## What was done instead, and what it leaves

The fences whose programs stop on purpose (`docs/language/04_execution_model.md:249`,
`:279`, `docs/language/en/04_execution_model.md:266`, `:296`) are marked `counter-example`,
which is true of them: each is written to end at its fault, and the text block is the run
that ends there. The listing in `docs/design/graph-store/plan-roundtrip.cn.md:808` is a
`fragment`: two functions quoted from a probe command, with the probe's own setup not on
the page. The tutorial's model example was a different fault of the page's own
(`docs/tutorial/tutorial.{cn,en}.md:1040`): its command fence stood two paragraphs below
the source fence, so no command line attached to it; the two fences are now adjacent, and
the example is run and its printed tensor compared, where before it was skipped.

That leaves the design-principles pair, which no page-side edit can resolve honestly.

## Two ways out, for the layer that owns the measuring instrument

1. **Judge "does not compile" by the output, not the exit code.** A dump on stdout with no
   `error[…]` / `[Error]` line and no fault diagnostic means the compile succeeded. The
   compile step already has the output in hand; the exit code adds nothing that the
   diagnostic does not say more precisely, and using it alone conflates "could not build"
   with "built, and the program's own result is not zero".
2. **Use a pass that does not evaluate the entry.** The pass table in `AGENTS.md` calls
   `std::gir` a translation pass that prints and returns an empty graph; if there is or can
   be a pass that prints and stops before the entry is finished, the compile step wants
   that pass, and every non-zero exit would then mean a real failure.

Either way the four `counter-example` markers and the one `fragment` become unnecessary
(the pages can go back to being compared), so whoever changes the instrument should re-read
those five fences at the same time.

## The list that stays

`PENDING_ALL` now holds 10 entries and `PENDING_MAX` is 10: the ceiling is flush with the
list, which is the shape the file asks for — adding an entry means raising the ceiling in
the same edit, visibly. The 10 are 2 waiting for the chapter 18 rule and 8 for range
values; none of them is behind because a page was left unwritten.