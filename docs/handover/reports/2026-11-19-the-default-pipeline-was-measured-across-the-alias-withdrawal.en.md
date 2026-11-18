# `std::opt` after the aliases were withdrawn: the graph is the same, measured

**What was claimed, and what was not.** `35bddb58c` merged the withdrawal of the twenty old sub-pass
spellings (`13 std::opt::<stage>` rows plus `7` inline-strategy and `std::inline` spellings) and said so
plainly: the before-and-after reading for `std::opt` had not been taken, and the substitutes in hand --
`fold.by_cpp`/`fold.by_camel` recorded at baseline, `std::opt ≡ std::cxx::opt` on one tree, and the
`["1","11","203","4"]` that `pass_surface.py` pins -- were not the same thing as one program measured on
two commits. The commit ended that paragraph with the sentence it should end with: *this cell is empty,
not green.*

**It is no longer empty.** The reading was taken after the merge, on two trees built from their own
commits, and the two documents are byte-identical.

```
baseline   1885b7ed0   build-info fingerprint 7f01cde3c234   1345 source files   BUILD_EXIT=0
lane       88f855ebb   build-info fingerprint 89c80a6172b3   1345 source files   BUILD_EXIT=0
subject    test/cases/opt/cxx_isolation/subject.cml           (5 graphs: __root__/main/chain/shared/work)
command    camel <subject> std::opt std::jgir canonical=true  (no normalizer)
```

```
                         sha256 of the whole stdout byte stream        bytes   graphs  canonical
1885b7ed0                d3e27bd6fc7329e83c6e9abc6f912954e54b5adc0e187a8181c2653a8f15b90e   14877   5   true
88f855ebb                d3e27bd6fc7329e83c6e9abc6f912954e54b5adc0e187a8181c2653a8f15b90e   14877   5   true
```

The two fingerprints differ, and that is the point of reporting them: this was not one binary run twice
against two checkouts. Each tree was built from its own commit, both report `1345 source files` and the
same options string, and both builds exited zero. The instrument itself raised a deliberate failure on
each side -- that is how a hash gets printed at all -- so the `fail: 1` in those two runs is the
instrument speaking, not a red.

**What this settles.** Withdrawing the aliases did not change what the default pipeline computes. The
twenty deleted rows were the only thing that went; `std::opt` is still registered
(`src/execute/pass.cpp:725`) and still aliases to `std::cxx::opt` (`:747`), and the two
`cxx_isolation.default_pipeline.*` cases keep that honest on every run thereafter.

**What it does not settle.** One subject file, five graphs. The claim this page supports is "the default
pipeline's output did not change on this subject", which is the claim the merge was blocked on. It is not
a claim about every program; a wider corpus would need its own two-tree measurement, and the instrument
for it is forty lines and does not belong in the tree.

**The two trees.** The baseline tree `_alias-base` was created at `1885b7ed0` after the coordinator
explicitly asked for it -- `AGENTS.md` otherwise forbids worktrees nobody asked for -- and was removed
with `git worktree remove` (no `--force`) once the reading was in; both sides' `git status --porcelain
--untracked-files=all` were empty before removal, and the temporary branch `lane-alias-base` went with
`branch -d`. The instrument and both run logs live in `D:\Temp` and in no commit.

**Why this is a page and not an amendment.** `develop` is never rewritten here. The merge commit says a
cell is empty; it was empty when it was written, and it is the honest record of what was known at that
moment. This page is the record of what was known an hour later. Two records, each true at its own time,
beat one commit message edited to look prescient.