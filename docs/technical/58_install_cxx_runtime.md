# The install carries its own C++ runtime

An install of Camel is started by a binary that links `libcamel.so`, which in turn links
`libstdc++.so.6`. The version of that library the binary was linked against is newer than the one
an older distribution ships, and the loader refuses a binary whose version requirements the library
it finds does not satisfy. This note records what the install does about it, why one half of the
answer has to be a launcher, and what the size costs.

The ruling it implements (language author, 2026-11-03 18:55) is "A, plus the C sentence": an install
finds its own runtime, no caller exports anything, and a runtime that is too old ends the run with
one sentence that says what is missing, what is there, and what to do.

## 1. What the install carries

Two things, and only two:

* **A relative rpath on every object that links `libcamel`.** `libcamel.so` carries
  `$ORIGIN:$ORIGIN/../libs` (`CMakeLists.txt`, `set_target_properties(libcamel ...)`), and every
  tool carries `$ORIGIN:$ORIGIN/libs:$ORIGIN/../libs` (`tools/libcamel_tool.cmake`,
  `target_link_libcamel_runtime`). The second entry is why `bin/camel` finds `libs/libstdc++.so.6`
  three directories up from itself, and the first is why a consumer that is not one of our tools --
  the Python binding, anything that loads the library directly -- resolves `libcamel`'s own
  dependencies from inside the install.
* **The toolchain's `libstdc++.so.6`, copied into `libs/`** by `scripts/collect-out.js`
  (`collectCxxRuntimeLibrary`). The copy is the file the compiler driver names, as that driver
  reports it (`<cxx> --print-file-name=libstdc++.so.6`), real path resolved: the driver that chose
  the library at link time is the one whose answer decides which version the binary needs.

What is **not** carried is equally deliberate: a link that resolved the library in a directory of
the distribution (`/lib`, `/lib32`, `/lib64`, `/usr/lib`, `/usr/lib32`, `/usr/lib64`, and every
directory below them -- `SYSTEM_LIBRARY_DIRS` in `scripts/collect-out.js`) is not copied, because
the machine that runs the install is the machine whose copy answered the link. The build says which
of the two happened:

```
[collect] Bundled libstdc++.so.6 from ~/camel-eval/conda/envs/clang/lib/gcc/x86_64-conda-linux-gnu/16.2.0/libstdc++.so.6.0.36: the binaries load it from libs/
[collect] Not bundling libstdc++.so.6: the link resolved it in /usr/lib/x86_64-linux-gnu, a directory of the distribution (SYSTEM_LIBRARY_DIRS)
```

Windows is untouched by either half: `libs/` there carries the Python runtime DLLs and the import
library, and `bin/camel.exe` is the binary itself, started directly.

## 2. Why the "too old" sentence cannot come from inside the program

The versions a binary requires from `libstdc++.so.6` are ELF version requirements: they are recorded
in `.gnu.version_r` and the dynamic loader checks them while it maps the object, before the
program's `main`. Measured on the Linux host on 2026-11-04, with an install copied to `/tmp` and its
`libs/libstdc++.so.6` replaced by the distribution's GLIBCXX_3.4.30:

```
/tmp/probe-oldlib/bin/camel: /tmp/probe-oldlib/bin/../libs/libstdc++.so.6: version `GLIBCXX_3.4.32' not found (required by /tmp/probe-oldlib/bin/camel)
... (seven lines, three for the executable and four for libcamel.so)
exit status 1
```

Not one line of that came from Camel. A check written into `main` would never run: the process is
ended before it. Nothing inside the process can precede the check, so the sentence has to come from
the only thing that runs before the process exists -- a launcher. (A `--version-script` cannot help
either: it decides which symbols an object *exports*, not which versions it *requires*.)

## 3. What the launcher does

On Linux, `scripts/collect-out.js` turns `bin/camel` into a POSIX `sh` script and renames the
binary to `bin/camel.real` (`scripts/cxx-runtime-guard.mjs`). The script:

* locates its own directory (following a symbolic link, so `ln -s <install>/bin/camel /usr/local/bin/camel`
  still finds the install), and `bin/camel.real` beside it;
* asks the loader the same question the kernel would -- `ld.so --list <binary>`, with the
  interpreter read from the binary's `PT_INTERP` at collect time -- because that maps exactly what
  an `exec` would map and names the file it picked, so the verdict cannot disagree with the run it
  is about to start;
* when that report is a `GLIBCXX_* not found` line, prints two lines and exits 1;
* otherwise `exec`s the binary, so arguments, exit status, signals and every other loader complaint
  are exactly what they were before the script existed. A launcher that cannot be written (not
  Linux, no ELF, no interpreter) leaves the install as it was.

The user sees, for a `libs/libstdc++.so.6` replaced by an older one:

```
camel: cannot start: this install needs the C++ runtime GLIBCXX_3.4.36, and /tmp/probe/libstdc++.so.6 has at most GLIBCXX_3.4.30.
camel: put a libstdc++.so.6 that has GLIBCXX_3.4.36 (the one the toolchain that built this install ships) into /tmp/probe/libs, and run this again.
```

and for an install whose `libs/` was not carried along at all -- the other way this happens -- the
same two lines, naming the distribution's copy the loader fell back to:

```
camel: cannot start: this install needs the C++ runtime GLIBCXX_3.4.36, and /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.30 has at most GLIBCXX_3.4.30.
camel: put a libstdc++.so.6 that has GLIBCXX_3.4.36 (the one the toolchain that built this install ships) into /tmp/probe/libs, and run this again.
```

Both are one sentence of fact and one of remedy, and both exit 1 -- the status the loader itself
uses, so a caller that already treated "the binary did not start" as a failure keeps doing so.

The cost is one extra process per `camel` start. Measured on the host at load 21: `camel.real
--version` 6.85 ms, `camel --version` 12.6 ms, so about 5.8 ms, on a command whose work is elsewhere
in the millisecond range only for `--version` itself.

**Only `camel` is wrapped.** It is the entry point every caller starts -- the test runner, the
scripts, the wheel's console entry, a user -- and it is the one the ruling names. The other tools in
`bin/` link the same library and would print the same loader lines if they were started first; making
each of them a launcher is a one-line addition in `scripts/collect-out.js` and deliberately not taken
here, because `lang-server` and `debugger` resolve their own location from `argv[0]` and there is no
reading that the extra two files per tool buy anything for the documented path.

## 4. What it costs

`libs/` grows by the size of the toolchain's library:

| file | bytes | MB (10^6) | MiB |
| --- | --- | --- | --- |
| `libs/libstdc++.so.6` | 23,874,544 | 23.9 | 22.8 |

`libs/` before this change held `libcamel.so` (20,130,880 B) and the Python bridge; with the runtime
it holds 44.0 MB (43,005,424 B for those two files alone). The ruling's "24 MB" is this number, and
a wheel gains the same bytes before compression (the `docs/handover/pitfalls.cn.md` row estimates
+8 MB in the wheel) -- the release note for a version that first ships it has to say so.

## 5. Where the two halves live

| what | where |
| --- | --- |
| the rpath entries | `CMakeLists.txt` (`libcamel`), `tools/libcamel_tool.cmake#target_link_libcamel_runtime` (tools) |
| the copy into `libs/`, and the distribution-directory rule | `scripts/collect-out.js#collectCxxRuntimeLibrary`, `#isSystemLibraryDir` |
| the launcher | `scripts/cxx-runtime-guard.mjs#installCxxRuntimeGuard`, `#launcherText`, `#elfInterpreter` |
| the check the launcher makes | `test/plans/feat/tools/install_runtime.plan.toml` |