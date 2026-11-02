# The switch that reaches no target

`camel-remote.ps1 -StackTrace` exists, `-DCAMEL_STACK_TRACE=ON` reaches cmake, the option reads `ON` in
`CMakeCache.txt`, and **no target is compiled with the definition**. This page records where the chain
stops, because the next person will otherwise replay six rounds of it.

## The five readings of "it is on"

A build-configuration change is only a reading after five separate facts are each observed:

1. **the source has it** -- ``CAMEL_STACK_TRACE` in `CMakeLists.txt` and `assert.cpp``;
2. **the command has it** -- `[STEP] Running: cmake .. ... -DCAMEL_STACK_TRACE=ON`;
3. **the configure step ran** -- `[build] configure: running` (added for exactly this question);
4. **the artifact has it** -- and *the right artifact*: `Ninja Multi-Config` writes its compile lines to
   `build-Release.ninja`, **not** to `build.ninja`;
5. **the process received it** -- only then may anyone say "the switch is on".

**Missing any one of the five, "on" is an intention.** Tonight the chain stops at 4.

## Where it stops

    CAMEL_STACK_TRACE:BOOL=ON            in build/CMakeCache.txt
    grep -c CAMEL_STACK_TRACE build/build.ninja         -> 0
    grep -c CAMEL_STACK_TRACE build/build-Release.ninja -> 0
    grep -c rdynamic build/build.ninja                  -> 0

`option()` is on, `add_compile_definitions` sits after `project()` and before every `add_library`, the
configure step demonstrably re-ran (the cache was deleted and rebuilt), and the definition reaches no
target. **The switch is not wired through.** The first three facts are readings; the fourth is the wall.

## "I changed the source" is not "the artifact used my source"

Four instances from one night, all the same shape, each one a false reading before it was a finding:

1. **`-rdynamic` vanished from the link line** -- a link option that had been added was not in the
   rebuilt line, and every later "only `assert.cpp` changed" comparison still carried it.
2. **Deleting `CMakeCache.txt` is not "the configure step re-runs"** -- the cache was dropped and the
   build finished in seconds without configuring.
3. **Changing `CMakeLists.txt` does not regenerate `build.ninja`** -- with the same `-D` value as the
   cache, CMake decides there is nothing to regenerate and the old ninja file stands.
4. **The wrong artifact was being read** -- `build.ninja` is not where a multi-config generator puts its
   compile lines, so `grep` answered 0 for four consecutive rounds. **Four consecutive false zeros**:
   the reading was stable, plausible, and not a reading at all.

The rule that covers all four: **only a reading taken after the configuration is actually rebuilt counts
for a build-configuration change; an incremental build can neither prove nor disprove one.**

## Next

To finish this, the configure step's own output has to be visible first (`build/configure.log`, or the
last 200 lines printed on failure) -- the reason the chain could not be followed is that a failed or
skipped configure left no trace beyond "the build was fast".
