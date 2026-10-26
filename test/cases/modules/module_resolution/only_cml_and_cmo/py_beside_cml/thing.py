#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The Python side of the same name: a Python module is not a Camel module.

This file has the stem `thing` and sits next to `thing.cml`. It is Python, and it is what the
lookup used to read as the module `thing` -- `thing.py:2:17`, "Unexpected 'the'; expected 'func'",
on its own copyright line -- whenever the directory happened to list it before `thing.cml`.
"""

value = 7