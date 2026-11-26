"""A block's node answers the body's value when a scheduler runs the program it stands in.

`test::block_body` compiles a body as a program of its own, registers it in the context's store, and
puts one node of that block in the exit of the program's entry. Everything the block_body case reads
happens inside the pass; nothing there runs the entry program, so what a user's pipeline does with
the node -- the scheduler runs it, its operator is resolved through the executor manager by the node's
URI, and the kernel runs the body's graph on the context the block was pinned to -- was unmeasured.

The reading is the process exit code, which is the value `main` returns (design principles 9.8). The
source returns 41 and the block's body is `x + 1` over the value the exit hands it, so a run that goes
through the block ends 42 and a run that does not ends 41. The two numbers differ by exactly the
body's arithmetic, and neither is an error: a kernel that found no program for its node answers
-1, -2 or -3, which the process reports as 255, 254 or 253, and a failed result ends 1.

Two schedulers, because the node's identity is handed to the kernel by the back end that runs it:
`std::nvm` (the reference executor) and `std::fvm` (the bytecode one). The control is the same source
through the same scheduler with no pass, which must end 41 -- the entry's own value -- so 42 means the
exit read the block and not something else.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

EXE, ARGV = camel_from_argv(ROOT)
SOURCE = str(ROOT / "test" / "cases" / "runtime" / "block_scheduler" / "returns_41.cml")
BODY_PASS = "test::block_body"


def invoke(*args):
    return subprocess.run([EXE, *ARGV, *args], cwd=ROOT, capture_output=True, text=True)


def exit_code(p, what):
    # The pass's own output is on stdout when it ran; a diagnostic is on stderr when a run failed.
    where = (p.stdout or "") + (p.stderr or "")
    if p.returncode not in (41, 42):
        raise AssertionError(f"{what}: expected 41 (no block) or 42 (the block's answer), got {p.returncode}\n{where[-1200:]}")
    return p.returncode


plain_nvm = invoke(SOURCE, "std::nvm")
block_nvm = invoke(SOURCE, BODY_PASS, "std::nvm")
plain_fvm = invoke(SOURCE, "std::fvm")
block_fvm = invoke(SOURCE, BODY_PASS, "std::fvm")

# The control: without the pass the exit reads what the function returned.
assert exit_code(plain_nvm, "std::nvm") == 41, plain_nvm.stdout
assert exit_code(plain_fvm, "std::fvm") == 41, plain_fvm.stdout
# And with it, the value the block answered: 41 in, 42 out, through both schedulers.
assert "into the exit of <main>" in block_nvm.stdout, block_nvm.stdout[-1200:]
assert "BLOCK_CALL body block:" in block_nvm.stdout, block_nvm.stdout[-1200:]
assert exit_code(block_nvm, "test::block_body std::nvm") == 42, block_nvm.stdout[-1200:]
assert exit_code(block_fvm, "test::block_body std::fvm") == 42, block_fvm.stdout[-1200:]

print(
    f"block_scheduler: plain_nvm={plain_nvm.returncode} block_nvm={block_nvm.returncode} "
    f"plain_fvm={plain_fvm.returncode} block_fvm={block_fvm.returncode}"
)
print("Block under a scheduler: PASS")