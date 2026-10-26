#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""A Python module with the stem `thing` and no `thing.cml` beside it.

It is a module of the Python package this directory belongs to, not a module of the Camel program:
reading it as Camel reports a syntax error on its own copyright line (`thing.py:2:17`,
"Unexpected 'the'; expected 'func'") instead of saying that the module was not found.
"""

value = 7