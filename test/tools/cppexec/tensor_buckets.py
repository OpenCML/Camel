#!/usr/bin/env python3
"""How the programs that wait for step 5 (external objects) split by the result type of the operator
that refuses them.

The question this answers: of the programs `std::cpp` refuses because a tensor had nowhere to live,
how many are refused by an operator whose result is a *native* array with a concrete shape, how many
by an operator of the `tensor` module (a runtime object), and how many look like both -- the last
group is the evidence a decision about representation needs.

It only measures: it runs the compiler on every case, reads the refusal, and buckets it. Nothing here
changes a translation.

Usage, from the root of a built tree:
    python3 test/tools/cppexec/tensor_buckets.py [--root .] [--json out.json]
"""

import argparse
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

# The operators whose result is a value `std::cpp` has no representation for: the ones the census
# counts as "waiting for step 5".
WAITING = (
    "tensor:ones",
    "tensor:zeros",
    "tensor:load_npy_f32",
    "tensor:reshape",
    "rand:uniform_array",
    "rand:normal_array",
)

# Operators of the `tensor` module: their semantics lives in the runtime module, so the value is a
# runtime object (a handle) rather than something the pass builds.
MODULE_OBJECT = re.compile(r"^tensor:")

# Operators that are not of the tensor module but hand back a shaped array: a native array with a
# shape the call states.
NATIVE_ARRAY = ("rand:uniform_array", "rand:normal_array")


def cases(root):
    for directory, _, files in os.walk(os.path.join(root, "test", "cases")):
        for name in sorted(files):
            if name.endswith(".cml"):
                yield os.path.join(directory, name)


def refusal_of(env, camel, path):
    try:
        run = subprocess.run([camel, path, "std::cpp"], capture_output=True, text=True,
                             timeout=180, env=env)
    except Exception:
        return None
    out = (run.stdout or "") + (run.stderr or "")
    if "TranslationRefused" not in out:
        return None
    line = next((l for l in out.splitlines() if "TranslationRefused" in l), "")
    body = line.split("refused the graph: ", 1)[-1].strip()
    for uri in WAITING:
        if "'" + uri + "'" in body:
            return uri
    return None


def bucket_of(uri):
    """Which kind of value the refusing operator hands back.

    `module` -- an operator of the tensor module: its semantics is the runtime's, so the value is the
    runtime's object.
    `native` -- an operator outside that module whose result is a shaped array the call states.
    `both`   -- the two descriptions fit at once: a tensor-module operator that hands back a shaped
                array, which is exactly the seam the decision has to be made on.
    """
    if uri in NATIVE_ARRAY:
        return "native"
    if MODULE_OBJECT.match(uri):
        # A tensor-module operator whose result type is a shaped array: the module owns the
        # semantics, the type says the value is an array with a shape. Both are true.
        return "both" if uri == "tensor:reshape" else "module"
    return "native"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--json")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", os.path.join(root, "out", "latest"))
    camel = os.path.join(env["CAMEL_HOME"], "bin", "camel")

    found = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for path, uri in zip(cases(root), pool.map(lambda p: refusal_of(env, camel, p), cases(root))):
            if uri is not None:
                found.append((os.path.relpath(path, root), uri))

    groups = {"native": [], "module": [], "both": []}
    for path, uri in found:
        groups[bucket_of(uri)].append({"case": path, "operator": uri})

    reading = {
        "refused_by_a_waiting_operator": len(found),
        "native_shaped_array": len(groups["native"]),
        "tensor_module_object": len(groups["module"]),
        "both_descriptions_fit": len(groups["both"]),
        "by_operator": {},
    }
    for _, uri in found:
        reading["by_operator"][uri] = reading["by_operator"].get(uri, 0) + 1
    reading["both"] = groups["both"]

    print(json.dumps({k: v for k, v in reading.items() if k != "both"}, indent=2, sort_keys=True))
    print("both_descriptions_fit_list:")
    for row in groups["both"]:
        print("  %-64s %s" % (row["case"], row["operator"]))

    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(reading, fh, indent=1, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())