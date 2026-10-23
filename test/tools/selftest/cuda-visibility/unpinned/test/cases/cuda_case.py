# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""A CPU-only PyTorch program, as the fixture case of the unpinned plan.

test/tools/selftest/cuda-visibility/unpinned/test/plans/unpinned.plan.toml is the plan the guard
must refuse: one test reaches this file through a node harness, which the runner's Python default
does not wrap, and another names a card in its own `envs` without declaring that it wants one.
Nothing runs this file: the guard reads it.
"""

import torch

model = torch.nn.Linear(2, 2)
torch.optim.Adam(model.parameters(), lr=0.1).step()
print("step ok")