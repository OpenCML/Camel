"""A block's node answers the body's value when a scheduler runs the program it stands in.

`test::block_body` compiles a body as a program of its own, registers it in the context's store, and
puts one node of that block in the exit of the program's entry. Everything the block_body case reads
happens inside the pass; nothing there runs the entry program, so what a user's pipeline does with
the node -- the scheduler runs it, its operator is resolved through the executor manager by the node's
URI, and the kernel runs the body's graph on the context the block was pinned to -- was unmeasured.

The reading is the value the entry writes to `-o` (design principles 9.8: `main` is an ordinary
function, its value is a value of its own and is written to the output; the exit code is only what
`exit(code)` gives, so it cannot carry the answer). The source returns 41 and the block's body is
`x + 1` over the value the exit hands it, so a run that goes through the block writes 42 and a run
that does not writes 41. The two numbers differ by exactly the body's arithmetic, and neither is an
error: a kernel that found no program for its node answers -1, -2 or -3, which the output would carry
as those numbers, and a failed run ends 1 with no value written.

Two schedulers, because the node's identity is handed to the kernel by the back end that runs it:
`std::nvm` (the reference executor) and `std::fvm` (the bytecode one). The control is the same source
through the same scheduler with no pass, which must write 41 -- the entry's own value -- so 42 means
the exit read the block and not something else.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

EXE, ARGV = camel_from_argv(ROOT)
SOURCE = str(ROOT / "test" / "cases" / "runtime" / "block_scheduler" / "returns_41.cml")
BODY_PASS = "test::block_body"


def value_of(*args):
    """Run the source and read the value the entry wrote to `-o`."""
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "result.txt"
        p = subprocess.run([EXE, *ARGV, *args, "-o", str(out)], cwd=ROOT, capture_output=True, text=True)
        # The pass's own output is on stdout when it ran; a diagnostic is on stderr when a run failed.
        where = (p.stdout or "") + (p.stderr or "")
        if p.returncode != 0:
            raise AssertionError(f"{' '.join(args)}: the run ended {p.returncode}, expected 0\n{where[-1200:]}")
        if not out.is_file():
            raise AssertionError(f"{' '.join(args)}: no value was written to -o\n{where[-1200:]}")
        return p, out.read_text().strip()


# The control: without the pass the exit reads what the function returned.
plain_nvm, plain_nvm_value = value_of(SOURCE, "std::nvm")
plain_fvm, plain_fvm_value = value_of(SOURCE, "std::fvm")
assert plain_nvm_value == "41", plain_nvm_value + plain_nvm.stdout
assert plain_fvm_value == "41", plain_fvm_value + plain_fvm.stdout
# And with it, the value the block answered: 41 in, 42 out, through both schedulers.
block_nvm, block_nvm_value = value_of(SOURCE, BODY_PASS, "std::nvm")
block_fvm, block_fvm_value = value_of(SOURCE, BODY_PASS, "std::fvm")
assert "into the exit of <main>" in block_nvm.stdout, block_nvm.stdout[-1200:]
assert "BLOCK_CALL body block:" in block_nvm.stdout, block_nvm.stdout[-1200:]
assert block_nvm_value == "42", block_nvm_value + block_nvm.stdout[-1200:]
assert block_fvm_value == "42", block_fvm_value + block_fvm.stdout[-1200:]

print(
    f"block_scheduler: plain_nvm={plain_nvm_value} block_nvm={block_nvm_value} "
    f"plain_fvm={plain_fvm_value} block_fvm={block_fvm_value}"
)
print("Block under a scheduler: PASS")