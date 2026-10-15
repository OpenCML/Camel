#!/usr/bin/env python3
"""Census of `any` in the repository (docs/design/any-typing, section 1).

Run from the repository root:

    python3 docs/design/any-typing/tools/any_census.py            # summary
    python3 docs/design/any-typing/tools/any_census.py --list     # every site

It counts, separately:
  1. `any` written as a type in Camel source (.cml), comments and string literals removed;
  2. `Type::Any()` / `TypeCode::Any` in C++ (.cpp/.h) under src, include, modules, tools;
  3. the word `any` in a type position inside a C++ string literal that is a signature text
     (contains `=>`);
  4. operators whose type rule is a C++ function (DynamicFuncTypeResolver) versus a fixed
     signature (StaticFuncTypeResolver): the former accept "whatever the rule accepts" without
     the word `any` appearing anywhere.
"""
import os
import re
import sys

ROOT = os.getcwd()
# test/results is the test runner's output; any-typing and cpp-memory are the two reports written
# after this census (their examples use `any` on purpose).
SKIP_DIRS = {".git", "node_modules", "third_party", "out", "build", "tmp", "dist", "results",
             "any-typing", "cpp-memory"}

CML_ROOTS = ["stdlib", "test", "examples", "exemplars", "benchmarks", "docs", "modules", "hello.cml"]
CPP_ROOTS = ["src", "include", "modules", "tools"]


def walk(roots, exts):
    for root in roots:
        path = os.path.join(ROOT, root)
        if os.path.isfile(path):
            if path.endswith(exts):
                yield root
            continue
        for d, dirs, files in os.walk(path):
            dirs[:] = [x for x in dirs if x not in SKIP_DIRS]
            for f in files:
                if f.endswith(exts):
                    yield os.path.relpath(os.path.join(d, f), ROOT).replace("\\", "/")


def strip_cml(text):
    """Blank out comments and string literals, keeping line structure."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "'\"`":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + "".join(ch if ch == "\n" else " " for ch in text[i + 1:j]) + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


# `any` in a type position: after ':' or '|' or 'as' or '=>' or '<' or '(' ... followed by a
# type continuation. A function named `any` (stdlib/array.cml) is `any(` or `any<` after
# `func`, `->`, `{`, `,` in an import list: those are excluded by requiring a type context
# on the left.
TYPE_LEFT = re.compile(r"(:|\||\bas|=>|\btype\s+\w+\s*=)\s*$")


def cml_sites(path):
    with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as fh:
        text = strip_cml(fh.read())
    for ln, line in enumerate(text.split("\n"), 1):
        for m in re.finditer(r"\bany\b", line):
            left = line[:m.start()]
            right = line[m.end():]
            if TYPE_LEFT.search(left):
                kind = "return" if re.search(r"\)\s*:\s*$", left) else (
                    "cast" if re.search(r"\bas\s*$", left) else (
                        "union" if left.rstrip().endswith("|") or right.lstrip().startswith("|")
                        else "param-or-let"))
                yield ln, kind, line.strip()


def cpp_sites(path):
    with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as fh:
        lines = fh.read().split("\n")
    for ln, line in enumerate(lines, 1):
        if re.search(r"Type::Any\(\)", line):
            yield ln, "Type::Any()", line.strip()
        if re.search(r"TypeCode::Any\b", line):
            yield ln, "TypeCode::Any", line.strip()
        for m in re.finditer(r'"([^"\n]*)"', line):
            s = m.group(1)
            if "=>" in s and re.search(r"(:|=>|\|)\s*any\b", s):
                yield ln, "signature-text", line.strip()


def resolver_counts():
    static = dynamic = 0
    for path in walk(CPP_ROOTS, (".cpp", ".h")):
        with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        static += len(re.findall(r"StaticFuncTypeResolver::create\(", text))
        dynamic += len(re.findall(r"DynamicFuncTypeResolver::create\(", text))
    return static, dynamic


def bucket(path):
    parts = path.split("/")
    if parts[0] == "stdlib":
        return "stdlib/signatures" if len(parts) > 1 and parts[1] == "signatures" else "stdlib"
    if parts[0] == "test":
        return "test"
    if parts[0] == "docs":
        return "docs"
    return parts[0]


def main():
    show = "--list" in sys.argv
    print("== 1. `any` written as a type in .cml ==")
    total = {}
    for path in sorted(walk(CML_ROOTS, (".cml",))):
        for ln, kind, line in cml_sites(path):
            key = (bucket(path), kind)
            total[key] = total.get(key, 0) + 1
            if show:
                print(f"  {path}:{ln}: [{kind}] {line[:110]}")
    for key in sorted(total):
        print(f"  {key[0]:20s} {key[1]:14s} {total[key]}")
    print(f"  total {sum(total.values())}")
    ncml = sum(1 for _ in walk(CML_ROOTS, (".cml",)))
    print(f"  (.cml files scanned: {ncml})")

    print("== 2/3. `any` in C++ ==")
    total = {}
    for path in sorted(walk(CPP_ROOTS, (".cpp", ".h"))):
        for ln, kind, line in cpp_sites(path):
            key = (path, kind)
            total[key] = total.get(key, 0) + 1
            if show:
                print(f"  {path}:{ln}: [{kind}] {line[:110]}")
    bykind = {}
    for (path, kind), v in sorted(total.items()):
        print(f"  {path:48s} {kind:16s} {v}")
        bykind[kind] = bykind.get(kind, 0) + v
    print("  by kind:", ", ".join(f"{k} {v}" for k, v in sorted(bykind.items())))

    print("== 4. type rules ==")
    s, d = resolver_counts()
    print(f"  StaticFuncTypeResolver::create  {s}")
    print(f"  DynamicFuncTypeResolver::create {d}")


if __name__ == "__main__":
    main()
