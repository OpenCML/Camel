# The exit code the driver gives a `main` that returns a value

**Corrected.** An earlier version of this page concluded that the checker's verdict was
wrong and that the way out was to change the checker. Principle 9.8 (`docs/design-principles.cn.md:706`,
decided by the language author on 11-26) settles it the other way: `main` is an ordinary
function, the program has no result of its own, and the exit code is an explicit effect
(`process::exit(code)`) that is never expressed through a return value. So the driver's
behaviour recorded below is the defect, and the checker is right to expect a snippet that
only has to compile to end with `0`. The readings and the three refusals stand; the
conclusion is replaced.

## What is left red

Two reader-facing fences are reported as `does not compile`: `docs/design-principles.cn.md:131`
and `docs/design-principles.en.md:134`. Both hold the same example, a graph of two nodes
in which `main` returns `b`, and `b` is `3` (`log(1)` → `2`, `log(2)` → `3`). The example
compiles. The message the checker prints is not even a diagnostic:

```
does not compile: digraph GraphIR {
```

That line is the head of the DOT dump the graph pass prints on stdout, which is where
`firstDiagnostic` falls back when the output carries no diagnostic at all
(`scripts/check-doc-snippets.mjs:637`). So the verdict came from the exit code alone.

## The reading

`camel <file> std::gir` on three programs that differ only in what `main` returns:

| program | stdout | exit |
| --- | --- | --- |
| `func main() => int { return 0 }` | the DOT dump | 0 |
| `func main() => int { return 7 }` | the DOT dump | 7 |
| the design-principles example (`main` returns `3`) | the DOT dump | 3 |

The exit code is the returned value. Under principle 9.8 it must not be: a program that
never calls `process::exit` exits `0`, whatever `main` returns.

## Why the page cannot be changed instead

The checker only asks that a snippet without a text block compile:

> Otherwise the snippet only has to compile: parse, GCT and GIR, asserted with
> `camel <file> std::gir`.
> — `scripts/check-doc-snippets.mjs:25`

Three page-side routes exist and each was refused, because each would put something false
on the page:

- **`fragment`** — "a piece of a program"; the reason must name what is missing. Nothing
  is missing from this fence: it is a whole program, and it is right.
- **`counter-example`** — "deliberately wrong". This example is not wrong, it is the
  chapter's illustration of how a graph is entered. The marker would also demand a label
  word on the page (`反例`, `错误`, `wrong`, `does not compile`, …), so the page would have
  to say something untrue a second time.
- **`compile-only`** — a text block that is not this run's output; the fence is still
  compiled, so the exit code is still read. It changes nothing.

A fourth route exists and was rejected as well: adding a `// camel …` line to the fence so
the run step takes over. The run step expects exit `0` unless the text block declares a
diagnostic, so the same non-zero result would fail there instead — and the fence is
presented as the content of a file the page links, so it must not gain a line the file does
not have.

## The fix is the driver, and one paragraph of the CLI page

The work is `task-272`: make the driver stop deriving the exit code from `main`'s return
value, so that a program which never calls `process::exit` exits `0` and the returned value
is written out as the ordinary result it is (9.8 line 706). `docs/cli.md:148`–`:151` still
state the superseded rule ("Only a `main` that declares its return type has a result … the
value is the exit code, as for any `main`"), so that paragraph moves with the driver; the
principle is normative and the document is the side that has fallen behind.

When that lands, this pair of entries is expected to turn green without any page edit, and
the check that was red here becomes the evidence that the driver changed.

## What the five marked fences owe, once the driver changes

Around this pair, five fences were marked while the reading was being taken, and the driver
change does not retire them:

- `docs/language/04_execution_model.md:249`, `:279` and
  `docs/language/en/04_execution_model.md:266`, `:296` are `counter-example`: each program
  is written to stop at a fault (an index past the end, a recursion over the limit) and the
  text block is the run that ends there. A fault still leaves a non-zero exit under 9.8, so
  a graph pass still cannot finish them and the markers stay.
- `docs/design/graph-store/plan-roundtrip.cn.md:808` is a `fragment`: two functions quoted
  from a probe command, with the probe's own setup not on the page. Unrelated to the exit
  code.

The tutorial's model example was a fault of the page's own
(`docs/tutorial/tutorial.{cn,en}.md:1040`): its command fence stood two paragraphs below the
source fence, so no command line attached to it; the fences are now adjacent, and the example
is run and its printed tensor compared, where before it was skipped.

After the driver change the five fences above should be re-measured: if the `std::gir` step
still ends non-zero for the four `counter-example` ones, their markers are right and stay;
only the design-principles pair is expected to move.

## The list that stays

`PENDING_ALL` holds 10 entries and `PENDING_MAX` is 10, deliberately left flush with the
list: adding an entry means raising the ceiling in the same edit, visibly. The 10 are 2
waiting for the chapter 18 rule and 8 for range values; none of them is behind because a
page was left unwritten.