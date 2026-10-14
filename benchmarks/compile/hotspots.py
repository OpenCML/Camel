#!/usr/bin/env python3
r"""Where a Camel run spends its time and makes its allocations, by function and by cause.

    python3 benchmarks/compile/hotspots.py cpu    FILE [PASS...] [--runs N] [--hz 1000] [--out DIR]
    python3 benchmarks/compile/hotspots.py allocs FILE [PASS...] [--every 17] [--out DIR]
    python3 benchmarks/compile/hotspots.py report SAMPLES_FILE...      saved sample files again

The default pass list is `std::null` (compile, execute nothing). Both commands run camel
under test/tools/allocshim: `cpu` with its sampling profiler (a CPU-time timer, one stack
per tick), `allocs` with one stack per N-th allocation. The stacks are resolved with
addr2line against the binaries the process had mapped, and summed three ways:

  by phase     which part of the pipeline the sample belongs to: the innermost frame that
               names one (the AST builder, the GCT builder, the GIR builder, the graph
               encoder, a pass, ...), so that a module compiled while another one imports it
               counts under its own phases
  by cause     what kind of work the innermost frames were doing: allocating or freeing,
               counting references, copying or comparing strings, looking up a hash table,
               comparing types, deriving source origins, a dynamic cast, ...
  by function  self (innermost frame) and inclusive (anywhere in the stack) per function

Reading the tables.

  * A release build inlines: a `shared_ptr` copy or a `std::string` comparison that was
    inlined is counted under the function it was inlined into, so the cause table is a lower
    bound for those causes. What it shows reliably is everything that ends in a call: the
    allocator, hash and compare helpers, `__dynamic_cast`, the release of a `shared_ptr`.
  * The C library of the container has no symbol table for its internal functions, so a
    frame inside it cannot be named. Allocation is recognised by the shim's own `malloc` /
    `free` frame further out (that includes the page faults of fresh heap memory, which is
    where most of "allocate" goes on a large input); any other sample that ends inside the C
    library is listed as "libc (memcpy, memset, ...)" under the function that called it.
  * The cause of a CPU sample is decided by its innermost frame outside the C library.
  * Stacks are cut at 48 frames, innermost first; the phase is decided by the innermost
    frames, so a deep recursion loses nothing that the tables use.
  * The profiling timer cannot tick faster than the kernel's own timer (250 Hz in the
    container this was written in), whatever --hz asks for: add runs to get more samples.
  * The header gives the user and system CPU time of the runs: system time is almost all
    page faults and `brk`/`mmap`, i.e. the cost of memory touched for the first time.

Linux only (LD_PRELOAD, setitimer, /proc/self/maps).
"""

from __future__ import annotations

import argparse
import collections
import re
import resource
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Tuple

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import bench  # noqa: E402

# The first frame, from the innermost outwards, that matches any pattern decides; among the
# patterns the first that matches the frame.
PHASES = [
    ("ast builder", r"camel::parse::ast::|CamelParser::buildAST"),
    ("lexer+cst", r"camel::parse::syntax::|CamelParser::buildCST"),
    ("parse (other)", r"camel::parse::"),
    ("gct builder", r"camel::compile::gct::"),
    ("gir encode", r"encodeToRuntimeGraph|EncodeSession|collectLiveDraftNodes|staticGraphRefsOf|planDraftPayload|materializeRuntimeRoot"),
    ("gir builder", r"camel::compile::gir::"),
    ("module load", r"Context::importModule|Context::tryLoadModule|loadCmoModule|getModulePathAndKind"),
    ("pass", r"applyPassesDetailed|Pass::apply"),
    ("startup", r"parseArgs|Context::create|ScopedRuntime|camel::initialize"),
    ("exit", r"__run_exit_handlers|_dl_fini|__cxa_finalize|exit$"),
]

# The cause of a sample: the first pattern that matches its innermost frame outside the C
# library (see classify()).
ALLOCATE, FREE, LIBC = "allocate", "free", "libc (memcpy, memset, ...)"
CAUSES = [
    (ALLOCATE, r"^(operator new|malloc|calloc|realloc|memalign|posix_memalign|aligned_alloc|note_alloc)"),
    (FREE, r"^(operator delete|free|note_free)"),
    ("shared_ptr refcount", r"_Sp_counted_base|_Sp_counted_ptr|__shared_count|__weak_count|"
                            r"enable_shared_from_this|__shared_ptr"),
    ("string hash", r"_Hash_bytes|std::hash<std::.*basic_string|_Hash_impl"),
    ("hash table", r"_Hashtable|_Map_base|unordered_map|unordered_set|_Hash_node|_Prime_rehash"),
    ("string copy/build", r"basic_string<.*>::(_M_construct|_M_assign|_M_append|_M_create|_M_mutate|_M_replace|"
                          r"append|assign|substr|basic_string|operator\+=|reserve|_M_dispose|~basic_string)|"
                          r"std::operator\+|__cxx11::to_string|std::to_string"),
    ("string compare", r"basic_string<.*>::compare|__memcmp|^memcmp|std::operator==.*basic_string|"
                       r"std::operator<=>.*basic_string|char_traits"),
    ("format / stream", r"std::format|std::__format|basic_ostream|basic_stringstream|basic_ostringstream|"
                        r"std::locale|ios_base|basic_stringbuf|vformat"),
    ("dynamic cast / rtti", r"__dynamic_cast|__cxxabiv1|type_info"),
    ("exception", r"_Unwind_|__cxa_throw|__cxa_allocate_exception|__gxx_personality"),
    ("std::any / function", r"std::any|_Any_data|std::function|_Function_handler|_M_manager"),
    ("source origins", r"camel::source::|SourceContext|OriginTable|SemanticBundle|registerGctSemantic|"
                       r"registerAstSemantic|deriveOrigin|setOriginFrom"),
    ("diagnostics", r"camel::core::error::|Diagnostic"),
    ("type system", r"camel::core::type::"),
    ("scope / name lookup", r"Scope<|Reference::|camel::core::module::Module::"),
    ("vector growth", r"std::vector<.*>::(_M_realloc|_M_fill_insert|_M_range_insert|_M_default_append|reserve|"
                      r"_M_realloc_append|_M_realloc_insert)"),
    ("tree / map (rb)", r"_Rb_tree"),
    ("memory spaces (mm)", r"camel::core::mm::"),
    ("compile-time data", r"camel::core::data::"),
]

# The frames a sample starts with that belong to the taking of the sample.
SHIM_FRAMES = re.compile(r"^(on_profile_tick|sample_site|note_alloc|backtrace|\(libc\)|\?\?)$")
ALLOC_ENTRY = re.compile(r"^(malloc|calloc|realloc|memalign|aligned_alloc|posix_memalign|operator new.*|"
                         r"std::__new_allocator.*|std::allocator.*|__gnu_cxx::new_allocator.*)$")


def classify(stack: List[Tuple[str, str]]) -> str:
    """The cause of a sample from its frames (function, binary), innermost first."""
    in_libc = False
    for name, binary in stack[:8]:
        if binary.startswith("libc.so"):
            in_libc = True          # unnamed internals: the frames further out decide
            continue
        name = strip_parameters(name)
        if binary.startswith("allocshim"):
            return FREE if name.startswith("free") or name == "note_free" else ALLOCATE
        hit = next((label for label, pattern in CAUSES if re.search(pattern, name)), None)
        if hit:
            return hit
        break
    return LIBC if in_libc else "own code"


def read_samples(path: Path) -> Tuple[str, List[Tuple[int, List[int]]], List[Tuple[int, int, int, str]]]:
    """Returns (kind, [(size, [address...])], [(start, end, file offset, path)])."""
    maps, samples, kind, in_samples = [], [], "alloc", False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("--- samples"):
            in_samples = True
            found = re.search(r"kind=(\w+)", line)
            kind = found.group(1) if found else "alloc"
            continue
        if in_samples:
            parts = line.split()
            if parts:
                samples.append((int(parts[0]), [int(x, 16) for x in parts[1:]]))
            continue
        found = re.match(r"([0-9a-f]+)-([0-9a-f]+) (\S+) ([0-9a-f]+) \S+ \S+\s+(/\S+)", line)
        if found and "x" in found.group(3):
            maps.append((int(found.group(1), 16), int(found.group(2), 16), int(found.group(4), 16),
                         found.group(5)))
    return kind, samples, maps


def symbolize(addresses: List[int], maps) -> Dict[int, Tuple[str, str]]:
    """(function, binary file name) of each address; the function is '??' when it is in no
    mapped binary or has no symbol."""
    by_file: Dict[str, List[Tuple[int, int]]] = collections.defaultdict(list)
    names: Dict[int, Tuple[str, str]] = {}
    for address in addresses:
        for start, end, offset, path in maps:
            if start <= address < end:
                by_file[path].append((address, address - start + offset))
                break
        else:
            names[address] = ("??", "")
    tool = shutil.which("addr2line") or shutil.which("llvm-addr2line")
    for path, items in by_file.items():
        binary = Path(path).name
        if tool is None or not Path(path).exists():
            for address, _ in items:
                names[address] = ("?? " + binary, binary)
            continue
        query = "\n".join(hex(offset) for _, offset in items) + "\n"
        done = subprocess.run([tool, "-f", "-C", "-e", path], input=query, capture_output=True, text=True)
        lines = done.stdout.splitlines()
        for index, (address, _) in enumerate(items):
            name = lines[2 * index] if 2 * index < len(lines) else "??"
            if binary.startswith("libc.so"):
                name = "(libc)"     # only exported symbols are known there: the nearest one misleads
            names[address] = (name if name != "??" else "?? " + binary, binary)
    return names


def strip_parameters(name: str) -> str:
    """A demangled name without its parameter list (and without what follows it)."""
    name = name.replace("(anonymous namespace)", "{anonymous}")
    depth = 0
    for index, char in enumerate(name):
        if char == "<":
            depth += 1
        elif char == ">":
            depth -= 1
        elif char == "(" and depth == 0 and not name.startswith("operator", max(0, index - 8)):
            return name[:index]
    return name


def shorten(name: str) -> str:
    name = strip_parameters(name)
    name = name.replace("std::__cxx11::", "std::")
    name = re.sub(r"<[^<>]*(<[^<>]*(<[^<>]*>[^<>]*)*>[^<>]*)*>", "<>", name)   # template arguments
    return name[-90:]


def resolved_stacks(path: Path) -> Tuple[str, List[Tuple[int, List[Tuple[str, str]]]]]:
    """The samples of one run with every frame as (function, binary), innermost first."""
    kind, samples, maps = read_samples(path)
    # Return addresses point after the call; the address before is inside the calling function.
    names = symbolize(sorted({a - 1 for _, frames in samples for a in frames}), maps)
    return kind, [(size, [names.get(a - 1, ("??", "")) for a in frames]) for size, frames in samples]


def analyze(paths: List[Path], top: int = 30, note: str = "") -> str:
    """The report over the samples of one or several runs of the same command."""
    kind, samples = "alloc", []
    for path in paths:
        kind, stacks = resolved_stacks(path)
        samples += stacks
    if not samples:
        return f"no samples in {', '.join(str(p) for p in paths)}\n"
    weight_name = "samples" if kind == "cpu" else "sampled allocations"
    phase_w, cause_w, self_w, incl_w = (collections.Counter() for _ in range(4))
    phase_cause: Dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    phase_self: Dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    bytes_by_phase = collections.Counter()
    total = 0
    for size, frames in samples:
        located = list(frames)
        if kind == "cpu":
            # The handler, then the signal trampoline (a frame of the C library): the
            # interrupted code starts after it.
            trampoline = next((i for i, (_n, binary) in enumerate(located) if binary.startswith("libc.so")), -1)
            located = located[trampoline + 1:]
        else:
            while located and SHIM_FRAMES.match(strip_parameters(located[0][0])):
                located.pop(0)
        if not located:
            continue
        stack = [name for name, _binary in located]
        total += 1
        # Phases are matched on the function alone: the parameter types of a GIR builder
        # function name the GCT, and would put it into the wrong phase.
        phase = "other"
        for frame in stack:
            hit = next((label for label, pattern in PHASES if re.search(pattern, shorten(frame))), None)
            if hit:
                phase = hit
                break
        if kind == "cpu":
            cause = classify(located)
            # A sample inside the C library is listed under the function that called into it.
            leaf = next((shorten(name) + (" -> libc" if index else "") for index, (name, binary)
                         in enumerate(located) if not binary.startswith("libc.so")), "(libc)")
        else:
            # For an allocation the interesting function is the one that asked for memory:
            # the first frame that is not the allocator, operator new or a standard container.
            leaf = next((shorten(f) for f in stack if not ALLOC_ENTRY.match(strip_parameters(f))
                         and not f.startswith("std::") and not f.startswith("__gnu_cxx::")), shorten(stack[0]))
            cause = next((label for label, pattern in CAUSES[2:]
                          for f in stack[1:5] if re.search(pattern, strip_parameters(f))), "own code")
        phase_w[phase] += 1
        cause_w[cause] += 1
        phase_cause[phase][cause] += 1
        self_w[leaf] += 1
        phase_self[phase][leaf] += 1
        bytes_by_phase[phase] += size
        for frame in {shorten(f) for f in stack}:
            incl_w[frame] += 1

    def pct(n: int) -> str:
        return f"{100.0 * n / total:.1f}%"

    out = [f"# {'CPU' if kind == 'cpu' else 'Allocation'} profile: {total} {weight_name}", ""]
    if note:
        out += [note, ""]
    rows = [[phase, str(n), pct(n)] + ([bench.fmt_count(bytes_by_phase[phase] * 1.0)] if kind != "cpu" else [])
            for phase, n in phase_w.most_common()]
    out += ["## By phase", "", bench.table(["phase", weight_name, "share"] + (["sampled bytes"] if kind != "cpu" else []),
                                           rows)]
    out += ["## By cause", "", bench.table(["cause", weight_name, "share"],
                                           [[c, str(n), pct(n)] for c, n in cause_w.most_common()])]
    for phase, n in phase_w.most_common(6):
        causes = phase_cause[phase].most_common(8)
        out += [f"### {phase} ({pct(n)} of all): causes", "",
                bench.table(["cause", "share of the phase"], [[c, f"{100.0 * k / n:.1f}%"] for c, k in causes])]
        funcs = phase_self[phase].most_common(12)
        out += [f"### {phase}: functions (self)", "",
                bench.table(["function", "share of the phase"], [[f"`{f}`", f"{100.0 * k / n:.1f}%"] for f, k in funcs])]
    out += [f"## Functions, self (top {top})", "",
            bench.table(["function", weight_name, "share"], [[f"`{f}`", str(n), pct(n)]
                                                              for f, n in self_w.most_common(top)])]
    out += [f"## Functions, inclusive (top {top})", "",
            bench.table(["function", weight_name, "share"], [[f"`{f}`", str(n), pct(n)]
                                                              for f, n in incl_w.most_common(top)])]
    return "\n".join(out)


def cmd_profile(args) -> int:
    home = bench.default_home()
    out_dir = Path(args.out).resolve() if args.out else Path(tempfile.mkdtemp(prefix="camel-profile-"))
    out_dir.mkdir(parents=True, exist_ok=True)
    shim = bench.build_shim(out_dir)
    if shim is None:
        print("profile: the shim could not be built (Linux with a C compiler is needed)", file=sys.stderr)
        return 2
    passes = args.passes or ["std::null"]
    source = str(Path(args.file).resolve())
    parts: List[Path] = []
    runs = args.runs if args.cmd == "cpu" else 1
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    for index in range(runs):
        part = out_dir / f"{args.cmd}.{index}.samples"
        env = bench.camel_env(home, {})
        env["LD_PRELOAD"] = str(shim)
        if args.cmd == "cpu":
            env["CAMEL_ALLOCSHIM_CPU"], env["CAMEL_ALLOCSHIM_HZ"] = str(part), str(args.hz)
        else:
            env["CAMEL_ALLOCSHIM_SITES"], env["CAMEL_ALLOCSHIM_EVERY"] = str(part), str(args.every)
        command = [str(bench.tool_path(home, "camel")), source] + passes
        done = subprocess.run(command, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                              cwd=str(Path(source).parent))
        if done.returncode != 0 or not part.exists():
            sys.stderr.write(done.stderr.decode("utf-8", "replace")[-2000:])
            print(f"profile: camel exited with {done.returncode}", file=sys.stderr)
            return 1
        parts.append(part)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    note = (f"`camel {Path(source).name} {' '.join(passes)}`, {runs} run(s): "
            f"{after.ru_utime - before.ru_utime:.2f} s user, {after.ru_stime - before.ru_stime:.2f} s system CPU time.")
    report = analyze(parts, args.top, note)
    (out_dir / f"{args.cmd}.md").write_text(report, encoding="utf-8", newline="\n")
    print(report)
    print(f"(samples: {', '.join(str(p) for p in parts)})", file=sys.stderr)
    return 0


def cmd_report(args) -> int:
    print(analyze([Path(p) for p in args.samples], args.top))
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("cpu", "allocs"):
        p = sub.add_parser(name)
        p.add_argument("file")
        p.add_argument("passes", nargs="*")
        p.add_argument("--out")
        p.add_argument("--top", type=int, default=30)
        p.add_argument("--runs", type=int, default=5, help="cpu: runs whose samples are added up")
        p.add_argument("--hz", type=int, default=1000, help="cpu: samples per second of CPU time")
        p.add_argument("--every", type=int, default=17, help="allocs: one sample per this many allocations")
        p.set_defaults(fn=cmd_profile)
    p = sub.add_parser("report")
    p.add_argument("samples", nargs="+")
    p.add_argument("--top", type=int, default=30)
    p.set_defaults(fn=cmd_report)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
