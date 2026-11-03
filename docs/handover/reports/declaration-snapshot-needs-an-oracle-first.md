# A declaration snapshot needs an oracle before it needs a reader

The author's 2026-11-02 12:10 ruling asked for a module cache, and the work splits into three
commits: the GCT round trip, the declaration snapshot, the graph cache. The first commit's
acceptance criteria (per-module `print(read(print(tree))) == print(tree)` over the standard
library, a magic-plus-version file that refuses a version it does not know, and order coverage
through `std::gir`'s sha256) all rest on a question nobody had asked: **can the GCT dump be
compared at all?**

Measured on 2026-11-02, binary `Camel v0.2.42 (Build 261102_123901_0bcd59bab_dirty)`, camel
`3ef60de3cc2b71b2`, host 88zjwei, `camel inspect --gct`:

1. **Raw, no.** Two runs of the same binary print different bytes for the same tree, because a
   constant is printed with its address: `DATA: 0x606086982580, int, 0L` on one run and
   `DATA: 0x55CEAF63A900, int, 0L` on the next. The prelude's dump reads sha256
   `e5155663c45f0c9c` and then `921ce59d68fe9431`.
2. **Normalized, yes.** With `0x[0-9A-Fa-f]+` replaced by one word, all 41 standard-library files
   (the 27 modules of `registry_all.cml`, their siblings, and `prelude`) print byte-identically
   across two runs: 41 same, 0 differ. The repository already knows this -- `test/tools/gctcheck/check.py`
   names the constant's address as "the one thing in it that differs from run to run" and replaces
   it before comparing two builds.
3. **`--gct` prints the entry module's tree only.** The per-module comparison the first criterion
   asks for is obtained by making each module the entry, which is how `gctcheck dump` walks the
   corpus; a library without `main` is a legal entry (case 5 of
   `test/cases/tools/inspect/gct_failure.mjs`). Of the 41 rows, 35 exit 0; six (`graph`, `json`,
   `nn`, `python`, `rewrite`, `tiles`) exit 1 as entries and print 8-71 lines of diagnostics
   instead of a tree. Their records are stable too, and they belong in the comparison: a snapshot
   that turned a refusal into a tree would be the worst kind of cache hit.
4. **What a reader must reproduce, in size:** 11,588 lines of dump over the 41 files. The largest
   are `rules` 2417, `opt` 1150, `math` 1117, `prelude` 964, `optim` 938, `array` 867; the median
   file is about 130 lines.

So both criteria are runnable as stated, with one addition each. The first compares **normalized**
dumps, and says so, because unnormalized the same binary fails against itself. The second takes
the `std::gir` sha256 on the **same** programs, because equality of `print` output proves only what
`print` shows -- the type's text, not the type's object -- and the graph dump is what covers the
part the text does not. The version criterion stays as written; it has no reading until the file
exists, since there is nothing yet to refuse.

What this changes: the first commit is not "write a reader", it is "make the dump an oracle, then
write a reader that reproduces it". The oracle exists, it is 41 files, and it is deterministic
once the address is gone. The expensive mistake this paragraph prevents is the obvious order --
writing a reader against raw dumps, running the corpus, and reading 41 of 41 as "differ" in a
format that could never have matched.