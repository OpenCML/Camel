"""What this process may see, printed so the plan can assert it.

Fixture of test/tools/selftest/cuda-visibility/visibility.plan.toml: the plan runner hides the
machine's CUDA devices from a Python child that declared nothing (docs/test-plans.md, "CUDA
visibility"), and a test that declares `cuda_devices = true` or names the variable itself sees
what the machine has. `<absent>` is a value the environment does not hold at all.
"""

import os

value = os.environ.get("CUDA_VISIBLE_DEVICES")
print(f"CUDA_VISIBLE_DEVICES=[{value if value is not None else '<absent>'}]")