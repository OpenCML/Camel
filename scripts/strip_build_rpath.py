#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Drop build-machine paths from the RUNPATH of the binaries that go into a wheel.

Every target that links libcamel carries an absolute entry CMake added for the build tree --
`build/Release` under wherever the build ran, e.g. `$ORIGIN:$ORIGIN/libs:$ORIGIN/../libs:
/src/build/Release`. That entry is a property of the machine that built the wheel, it is dead on
every other machine, and it is what `auditwheel repair` followed to find libcamel.so: the repaired
wheel ended up with a second, renamed copy of the library under `camel_lang.libs/`, and every
program that imported a module then ran with two libcamel images in one process and died in
teardown (`double free or corruption (!prev)`, exit 134).

What the installed layout needs is already there: the tools live in `<install>/bin/` next to
`<install>/libs/`, so `$ORIGIN` and `$ORIGIN/../libs` are the whole story. Everything absolute is
therefore dropped, in place: the entry is truncated inside `.dynstr`, which keeps every other
string's offset valid, and only ever gets shorter. The file on disk is what the loader reads, so
no rebuild and no `patchelf` is needed -- and the build tree itself is not touched, only the copy
the wheel is packed from.

Usage:
    strip_build_rpath.py <directory>          # rewrite, print what was dropped
    strip_build_rpath.py --check <directory>  # rewrite nothing; exit 1 if an absolute entry remains
"""

from __future__ import annotations

import os
import struct
import sys

PT_DYNAMIC = 2
PT_LOAD = 1
DT_NULL = 0
DT_STRTAB = 5
DT_RPATH = 15
DT_RUNPATH = 29

# Entries that describe where the object is rather than where it was built.
RELATIVE_PREFIXES = ("$ORIGIN", "${ORIGIN}", "@loader_path", "@executable_path", "@rpath")


class Elf:
    """Just enough of an ELF file to read and rewrite the dynamic string table."""

    def __init__(self, path: str) -> None:
        self.path = path
        with open(path, "rb") as handle:
            self.data = bytearray(handle.read())
        if self.data[:4] != b"\x7fELF":
            raise ValueError("not an ELF file")
        self.is64 = self.data[4] == 2
        self.little = self.data[5] == 1
        self.endian = "<" if self.little else ">"
        if self.is64:
            self.phoff = self._u(0x20, 8)
            self.phentsize = self._u(0x36, 2)
            self.phnum = self._u(0x38, 2)
        else:
            self.phoff = self._u(0x1C, 4)
            self.phentsize = self._u(0x2A, 2)
            self.phnum = self._u(0x2C, 2)

    def _u(self, offset: int, size: int) -> int:
        fmt = {2: "H", 4: "I", 8: "Q"}[size]
        return struct.unpack_from(self.endian + fmt, self.data, offset)[0]

    def _program_headers(self):
        for index in range(self.phnum):
            base = self.phoff + index * self.phentsize
            p_type = self._u(base, 4)
            if self.is64:
                yield p_type, self._u(base + 8, 8), self._u(base + 16, 8), self._u(base + 32, 8)
            else:
                yield p_type, self._u(base + 4, 4), self._u(base + 8, 4), self._u(base + 16, 4)

    def _vaddr_to_offset(self, vaddr: int) -> int | None:
        for p_type, p_offset, p_vaddr, p_filesz in self._program_headers():
            if p_type == PT_LOAD and p_vaddr <= vaddr < p_vaddr + p_filesz:
                return p_offset + (vaddr - p_vaddr)
        return None

    def rpath_entries(self):
        """(tag, string-table offset, text) for every DT_RUNPATH/DT_RPATH, or nothing."""
        dynamic = None
        for p_type, p_offset, _p_vaddr, p_filesz in self._program_headers():
            if p_type == PT_DYNAMIC:
                dynamic = (p_offset, p_filesz)
        if dynamic is None:
            return []
        entry_size = 16 if self.is64 else 8
        strtab = None
        found = []
        offset = dynamic[0]
        end = offset + dynamic[1]
        while offset < end:
            tag = self._u(offset, 8 if self.is64 else 4)
            value = self._u(offset + (8 if self.is64 else 4), 8 if self.is64 else 4)
            if tag == DT_NULL:
                break
            if tag == DT_STRTAB:
                strtab = value
            elif tag in (DT_RPATH, DT_RUNPATH):
                found.append((tag, value))
            offset += entry_size
        if strtab is None:
            return []
        file_offset = self._vaddr_to_offset(strtab)
        if file_offset is None:
            return []
        out = []
        for tag, string_offset in found:
            start = file_offset + string_offset
            stop = self.data.index(b"\x00", start)
            out.append((tag, start, bytes(self.data[start:stop]).decode("utf-8", "replace")))
        return out

    def rewrite(self, start: int, old: str, new: str) -> None:
        encoded = new.encode("utf-8")
        if len(encoded) > len(old):
            raise ValueError(f"the new RUNPATH is longer than the old one in {self.path}")
        self.data[start : start + len(old)] = encoded + b"\x00" * (len(old) - len(encoded))

    def save(self) -> None:
        with open(self.path, "wb") as handle:
            handle.write(self.data)


def wanted(entry: str) -> bool:
    return entry.startswith(RELATIVE_PREFIXES)


def main(argv: list[str]) -> int:
    check = "--check" in argv
    roots = [a for a in argv[1:] if not a.startswith("--")]
    if len(roots) != 1:
        print(__doc__.strip().splitlines()[-2:], file=sys.stderr)
        return 2

    files = 0
    dropped = 0
    offenders = []
    for root, _dirs, names in os.walk(roots[0]):
        for name in names:
            path = os.path.join(root, name)
            try:
                elf = Elf(path)
            except (ValueError, OSError):
                continue
            entries = elf.rpath_entries()
            if not entries:
                continue
            files += 1
            changed = False
            for _tag, start, text in entries:
                # An empty string is "no entry", not a bad one: a RUNPATH that was nothing but
                # absolute paths is left empty (or as `$ORIGIN`) by the rewrite above.
                parts = [p for p in text.split(":") if p]
                kept = [p for p in parts if wanted(p)]
                if len(kept) == len(parts):
                    continue
                gone = [p for p in parts if not wanted(p)]
                dropped += len(gone)
                offenders.append((path, gone))
                if not check:
                    new = ":".join(kept)
                    if not new and len("$ORIGIN") <= len(text):
                        new = "$ORIGIN"
                    elf.rewrite(start, text, new)
                    changed = True
                print(f"{os.path.relpath(path, roots[0])}: dropping {' '.join(gone)}")
            if changed:
                elf.save()

    if not dropped:
        print(f"strip_build_rpath: {files} ELF files with a RUNPATH, all entries relative")
        return 0
    verb = "would drop" if check else "dropped"
    print(f"strip_build_rpath: {verb} {dropped} absolute entries in {len(offenders)} files")
    return 1 if check else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))